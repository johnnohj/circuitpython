# Existing Semihosting Patterns in main.c

## TL;DR
**YES!** The WASM port already uses semihosting-like patterns extensively in `main.c`. We just need to **recognize and formalize** them!

---

## Pattern 1: External Call Depth Tracking (The Context Boundary)

**Location**: [main.c:47-70](main.c:47-70)

```c
// This counter tracks the current depth of calls into C code that originated
// externally, ie from JavaScript.
static size_t external_call_depth = 0;

void external_call_depth_inc(void) {
    ++external_call_depth;
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    if (external_call_depth == 1) {
        gc_collect_top_level();  // ← GC at top-level!
    }
    #endif
}

void external_call_depth_dec(void) {
    --external_call_depth;
}
```

**What this means**:
- ✅ Already tracking JavaScript → C boundary!
- ✅ When `external_call_depth == 1` → top-level call from host
- ✅ Triggers GC when returning to "host" (JavaScript)

**This IS semihosting!** It's managing the boundary between:
- **Target** (WASM/C Python VM)
- **Host** (JavaScript event loop)

**Semihosting equivalent**:
```c
// RISC-V debugger tracks when target makes semihosting call
// WASM tracks when JavaScript makes call into WASM

// Same pattern, different implementation!
```

---

## Pattern 2: mp_js_do_exec - The Semihosting Call

**Location**: [main.c:145-163](main.c:145-163)

```c
void mp_js_do_exec(const char *src, size_t len, uint32_t *out) {
    external_call_depth_inc();  // ← Mark entry from JavaScript (host)

    mp_parse_input_kind_t input_kind = MP_PARSE_FILE_INPUT;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Use lexer_dedent (we discovered this earlier!)
        mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(
            MP_QSTR__lt_stdin_gt_, src, len, 0
        );
        // ... execute Python code ...
        nlr_pop();
        external_call_depth_dec();  // ← Mark exit to JavaScript (host)
        proxy_convert_mp_to_js_obj_cside(ret, out);
    } else {
        // Exception handling across semihosting boundary
        external_call_depth_dec();
        proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
    }
}
```

**This is a textbook semihosting call!**

Compare to RISC-V semihosting:
```c
// RISC-V: Target asks host to print
mp_semihosting_write0("Hello from target");
// ↓ ebreak instruction
// ↓ Debugger (host) handles the call
// ↓ Returns to target

// WASM: JavaScript (host) asks target to execute
mp_js_do_exec("print('Hello from JavaScript')", ...);
// ↓ JavaScript calls into WASM
// ↓ WASM (target) executes Python
// ↓ Returns to JavaScript
```

**Key insight**:
- RISC-V semihosting: Target → Host calls
- WASM "reverse semihosting": Host → Target calls
- **Both need boundary tracking!**

---

## Pattern 3: mp_js_register_js_module - Host Providing Resources

**Location**: [main.c:109-114](main.c:109-114)

```c
void mp_js_register_js_module(const char *name, uint32_t *value) {
    mp_obj_t module_name = MP_OBJ_NEW_QSTR(qstr_from_str(name));
    mp_obj_t module = proxy_convert_js_to_mp_obj_cside(value);
    mp_map_t *mp_loaded_modules_map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_lookup(mp_loaded_modules_map, module_name, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = module;
}
```

**This is like semihosting's "host provides file system"!**

```c
// RISC-V semihosting: Host provides filesystem access
int fd = mp_semihosting_open("/host/file.txt", "r");

// WASM: Host (JavaScript) provides modules
mp_js_register_js_module("js", js_module_object);

// Python can now: import js
```

Both patterns:
- Host has resources
- Target needs access
- Interface to bridge them

---

## Pattern 4: pyexec Already Integrated!

**Location**: [main.c:172-179](main.c:172-179)

```c
void mp_js_repl_init(void) {
    pyexec_event_repl_init();  // ← Using shared/runtime/pyexec!
}

int mp_js_repl_process_char(int c) {
    external_call_depth_inc();  // ← Boundary tracking
    int ret = pyexec_event_repl_process_char(c);  // ← pyexec!
    external_call_depth_dec();
    return ret;
}
```

**We discovered pyexec earlier** - it's already being used!

This means:
- ✅ REPL uses `pyexec_event_repl_*` functions
- ✅ JavaScript can drive REPL character-by-character
- ✅ Boundary tracking already in place

**For the co-supervisor**: We can use the same pattern for code execution!

---

## Pattern 5: lexer_dedent Already Used!

**Location**: [main.c:150](main.c:150)

```c
mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(
    MP_QSTR__lt_stdin_gt_, src, len, 0
);
```

**We discovered lexer_dedent** and said we should use it - **IT'S ALREADY BEING USED!**

This means:
- ✅ No need to dedent in JavaScript
- ✅ Already handles indentation properly
- ✅ Proven to work

---

## Pattern 6: Exception Boundary (nlr_push/nlr_pop)

**Location**: Multiple functions

```c
void mp_js_do_exec(const char *src, size_t len, uint32_t *out) {
    external_call_depth_inc();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // ... Python execution ...
        nlr_pop();
        external_call_depth_dec();
        proxy_convert_mp_to_js_obj_cside(ret, out);  // Success path
    } else {
        // Exception!
        external_call_depth_dec();
        proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);  // Error path
    }
}
```

**This is exception marshaling across the semihosting boundary!**

Compare to semihosting:
```c
// RISC-V: Check if operation failed
if (mp_semihosting_iserror(result)) {
    int errno = mp_semihosting_errno();
    // Handle error
}

// WASM: Convert Python exception to JavaScript
proxy_convert_mp_to_js_exc_cside(exception, out);
// JavaScript can now handle the error
```

---

## Pattern 7: GC Management Across Boundary

**Location**: [main.c:182-203](main.c:182-203)

```c
#if MICROPY_GC_SPLIT_HEAP_AUTO

static bool gc_collect_pending = false;

// Don't collect anything.  Instead require the heap to grow.
void gc_collect(void) {
    gc_collect_pending = true;  // ← Defer GC!
}

// Collect at the top-level, where there are no root pointers from stack/registers.
static void gc_collect_top_level(void) {
    if (gc_collect_pending) {
        gc_collect_pending = false;
        gc_collect_start();
        gc_collect_end();
    }
}
```

**Called from**: [main.c:62-64](main.c:62-64)
```c
if (external_call_depth == 1) {
    gc_collect_top_level();  // ← Only at top-level!
}
```

**This is brilliant!**
- GC only runs when returning to JavaScript (host)
- Avoids GC during nested calls
- Ensures no JavaScript references on stack

**Semihosting pattern**: Don't do expensive operations during semihosting call, defer to safe points.

---

## What We Should Do: Formalize the Pattern

### Current State (Implicit Semihosting)
```c
// Scattered throughout code
void mp_js_do_exec(...) {
    external_call_depth_inc();
    // ... code ...
    external_call_depth_dec();
}

void mp_js_do_import(...) {
    external_call_depth_inc();
    // ... code ...
    external_call_depth_dec();
}

// etc.
```

### Proposed: Explicit Semihosting Layer
```c
// ports/wasm/wasm_semihosting.h

#define WASM_SEMIHOST_CALL_BEGIN() external_call_depth_inc()
#define WASM_SEMIHOST_CALL_END() external_call_depth_dec()

// Or better, a wrapper:
#define WASM_SEMIHOST_CALL(name, ...) \
    WASM_SEMIHOST_CALL_BEGIN(); \
    __VA_ARGS__; \
    WASM_SEMIHOST_CALL_END()
```

**Then our co-supervisor functions become**:
```c
// ports/wasm/supervisor/code_analysis.c

EMSCRIPTEN_KEEPALIVE
code_structure_t* analyze_code_structure(const char *code, size_t len) {
    WASM_SEMIHOST_CALL_BEGIN();

    // ... use lexer to analyze code ...

    WASM_SEMIHOST_CALL_END();
    return &result;
}
```

---

## Integration with Co-Supervisor

### The Complete Picture

```
┌─────────────────────────────────────────────────────────┐
│        JavaScript (Host/Supervisor)                      │
│  • Drives execution via requestAnimationFrame           │
│  • Provides resources (console, files, time)            │
│  • Tracks state in shared memory                        │
└────────────────┬────────────────────────────────────────┘
                 │
                 │ Semihosting Calls (already exist!)
                 │ • mp_js_do_exec(code)
                 │ • mp_js_repl_process_char(c)
                 │ • analyze_code_structure(code) ← NEW
                 ▼
┌─────────────────────────────────────────────────────────┐
│        WASM/C (Target/Python VM)                        │
│  • external_call_depth tracking                         │
│  • lexer_dedent for proper parsing                      │
│  • pyexec for execution                                 │
│  • nlr for exception handling                           │
│  • Deferred GC at top-level                             │
└─────────────────────────────────────────────────────────┘
```

### What's Already There
1. ✅ Boundary tracking (`external_call_depth`)
2. ✅ JavaScript → C calls (`mp_js_do_exec`, etc.)
3. ✅ Exception marshaling (`nlr_push/pop`)
4. ✅ Resource management (GC deferral)
5. ✅ Lexer integration (`lexer_dedent`)
6. ✅ Execution framework (`pyexec`)

### What We Need to Add
1. ⭕ **Formalize the pattern** - Create `wasm_semihosting.h`
2. ⭕ **Add supervisor calls** - `analyze_code_structure`, background callbacks
3. ⭕ **Document the architecture** - Make it explicit
4. ⭕ **Add C → JavaScript calls** - True bidirectional semihosting

---

## Comparison to RISC-V Semihosting

| Aspect | RISC-V Semihosting | WASM (Current) | WASM (Proposed) |
|--------|-------------------|----------------|-----------------|
| **Direction** | Target → Host | Host → Target | Bidirectional |
| **Boundary** | `ebreak` instruction | `external_call_depth` | Same + formal API |
| **Resources** | Host provides FS/console | JavaScript provides modules | JavaScript provides everything |
| **Exception Handling** | Error codes | `nlr` + proxy conversion | Same |
| **Execution Context** | Debugger pauses target | JavaScript calls into WASM | Same |
| **GC/Memory** | N/A | Deferred to top-level | Same |

**Key difference**: RISC-V is "target asks host for service", WASM is "host asks target to work"

**But the patterns are identical!**

---

## Recommended Next Steps

1. **Create `wasm_semihosting.h`** - Formalize the existing patterns
2. **Document boundary functions** - Make it clear what crosses the boundary
3. **Add new supervisor calls** - Build on existing pattern
4. **Use for co-supervisor** - JavaScript supervisor uses these calls

This is **not a rewrite** - it's **recognizing and formalizing** what's already there!

---

## Example: How Co-Supervisor Would Use Existing Patterns

```javascript
// supervisor.js

class CircuitPythonSupervisor {
    async loadCodeFromString(pythonCode) {
        // Use existing semihosting call!
        const mod = this.wasm._module;

        // This already tracks external_call_depth!
        const result = mod._mp_js_do_exec(codePtr, code.length, resultPtr);

        // Exception already marshaled by nlr!
        if (isException(result)) {
            this.handleException(result);
        }
    }

    runIteration() {
        // Use existing boundary tracking
        this.wasm.runPython(this.loopCode);

        // GC already deferred to top-level!
        // external_call_depth == 1 triggers gc_collect_top_level()
    }
}
```

**No major changes needed!** Just use what's already there and add:
- Code structure analysis (uses lexer - already integrated)
- Background callback bridge (uses existing boundary tracking)

---

## Summary

**YES**, main.c already uses semihosting patterns extensively:

1. ✅ **Boundary tracking** - `external_call_depth`
2. ✅ **Semihosting calls** - `mp_js_do_exec`, `mp_js_do_import`
3. ✅ **Exception marshaling** - `nlr` + `proxy_convert_*`
4. ✅ **Resource management** - GC deferral
5. ✅ **Infrastructure integration** - `pyexec`, `lexer_dedent`

**What we need**: Formalize and extend these patterns for the co-supervisor!

The architecture is **already there** - we just need to recognize it and build on it properly!
