# Co-Supervisor: Using Existing CircuitPython Infrastructure

## The User's Key Insight

> "Better would be to see if the supervisor interacts with pyexec or readline from ./circuitpython/shared, or ./circuitpython/py/parse, runtime, lexer. There already is lexer_dedent, come to think of it"

**This changes everything!** Instead of having JavaScript parse Python code with regex or even Python AST, we should **use the infrastructure that CircuitPython already has**.

---

## What's Already There

### 1. `lexer_dedent.c` - Automatic Dedenting
**Location**: `ports/wasm/lexer_dedent.c`

```c
// Calculates common whitespace and removes it
static size_t dedent(const byte *text, size_t len);

// Creates a lexer that automatically dedents while reading
mp_lexer_t *mp_lexer_new_from_str_len_dedent(qstr src_name,
    const char *str, size_t len, size_t free_len);
```

**What this means**:
- ✅ No JavaScript string manipulation for dedenting!
- ✅ Already integrated into lexer
- ✅ Handles indentation correctly

**Our current JavaScript supervisor** manually dedents:
```javascript
// supervisor.js - Lines 111-113
const dedentedLine = line.substring(baseIndent);
bodyLines.push(dedentedLine);
```

**We don't need this!** Just use `mp_lexer_new_from_str_len_dedent()`.

### 2. `pyexec` - Python Execution Framework
**Location**: `shared/runtime/pyexec.h`

```c
// Execute a file
int pyexec_file(const char *filename, pyexec_result_t *result);

// Execute from string
int pyexec_vstr(vstr_t *str, bool allow_keyboard_interrupt, pyexec_result_t *result);

// REPL modes
int pyexec_friendly_repl(void);
int pyexec_raw_repl(void);
```

**What this means**:
- ✅ Already knows how to run Python code
- ✅ Returns structured results (exception info, line numbers, etc.)
- ✅ Integrated with supervisor

**Our current approach**: JavaScript calls `wasm.runPython(code)` directly.

**Better approach**: Use `pyexec_vstr()` which integrates properly with the supervisor.

### 3. `mp_lexer` - The Python Lexer
**Location**: `py/lexer.h`

```c
typedef enum _mp_token_kind_t {
    MP_TOKEN_KW_WHILE,    // ← Can detect "while" loops!
    MP_TOKEN_KW_ASYNC,    // ← Can detect async def!
    MP_TOKEN_KW_AWAIT,    // ← Can detect await!
    MP_TOKEN_DEDENT,
    MP_TOKEN_INDENT,
    // ... 100+ token types
} mp_token_kind_t;

mp_lexer_t *mp_lexer_new_from_str_len(qstr src_name, const char *str, size_t len, size_t free_len);
void mp_lexer_to_next(mp_lexer_t *lex);  // Get next token
```

**What this means**:
- ✅ Can tokenize Python code
- ✅ Can detect `while`, `async def`, etc. **properly**
- ✅ Already handles all Python syntax edge cases

**Our current JavaScript parsing** uses regex:
```javascript
// supervisor.js - Lines 49-53
if (line.match(/^\s*while\s+(True|1|true)\s*:/)) {
    loopStartIdx = i;
    // ...
}
```

**Better approach**: Export lexer functions to JavaScript!

---

## The Revised Co-Supervisor Architecture

### Phase 1: Lexer-Based Code Analysis (Instead of Regex)

**Export lexer to JavaScript**:

```c
// ports/wasm/supervisor/code_analysis.c - NEW FILE

#include "py/lexer.h"
#include <emscripten.h>

typedef struct {
    bool has_while_true_loop;
    size_t while_true_line;
    bool has_async_def;
    bool has_await;
    bool has_asyncio_run;
} code_structure_t;

EMSCRIPTEN_KEEPALIVE
code_structure_t* analyze_code_structure(const char *code, size_t len) {
    static code_structure_t result = {0};

    // Create lexer
    mp_lexer_t *lex = mp_lexer_new_from_str_len(
        qstr_from_str("<analysis>"),
        code,
        len,
        0
    );

    result.has_while_true_loop = false;
    result.has_async_def = false;
    result.has_await = false;

    // Scan tokens
    while (lex->tok_kind != MP_TOKEN_END) {
        if (lex->tok_kind == MP_TOKEN_KW_WHILE) {
            // Next token should be True/1
            mp_lexer_to_next(lex);
            if (lex->tok_kind == MP_TOKEN_KW_TRUE ||
                (lex->tok_kind == MP_TOKEN_INTEGER && /* value == 1 */)) {
                // Next should be ':'
                mp_lexer_to_next(lex);
                if (lex->tok_kind == MP_TOKEN_DEL_COLON) {
                    result.has_while_true_loop = true;
                    result.while_true_line = lex->tok_line;
                }
            }
        }

        if (lex->tok_kind == MP_TOKEN_KW_ASYNC) {
            result.has_async_def = true;
        }

        if (lex->tok_kind == MP_TOKEN_KW_AWAIT) {
            result.has_await = true;
        }

        mp_lexer_to_next(lex);
    }

    mp_lexer_free(lex);
    return &result;
}
```

**JavaScript calls this**:
```javascript
// supervisor.js
async loadCodeFromString(pythonCode) {
    this.currentCode = pythonCode;

    // Use C lexer instead of JavaScript regex!
    const mod = this.wasm._module;
    const structPtr = mod._analyze_code_structure(codePtr, code.length);

    const structure = {
        hasLoop: mod.HEAPU8[structPtr] === 1,
        loopLine: mod.HEAPU32[(structPtr + 4) >> 2],
        hasAsync: mod.HEAPU8[structPtr + 8] === 1,
    };

    // Now we know the structure using REAL Python parsing!
    if (structure.hasLoop) {
        console.log(`Found while True: at line ${structure.loopLine}`);
    }
}
```

**Benefits**:
- ✅ No regex - uses actual Python lexer
- ✅ Handles all Python syntax correctly
- ✅ Can detect `async def`, `while True`, etc. reliably
- ✅ Returns line numbers for debugging

---

### Phase 2: Use `pyexec` Instead of Direct Execution

**Current approach**:
```javascript
// supervisor.js - Calls runPython directly
this.wasm.runPython(stmt);
```

**Better approach using pyexec**:

```c
// Export pyexec to JavaScript
EMSCRIPTEN_KEEPALIVE
int supervisor_exec_code(const char *code, size_t len, pyexec_result_t *result) {
    // Create vstr from code
    vstr_t vstr;
    vstr_init_fixed_buf(&vstr, len + 1, (char*)code);
    vstr.len = len;

    // Execute using pyexec infrastructure
    return pyexec_vstr(&vstr, true, result);
}
```

**JavaScript**:
```javascript
// supervisor.js
executeStatement(stmt) {
    const resultPtr = mod._malloc(sizeof_pyexec_result_t);
    const ret = mod._supervisor_exec_code(stmtPtr, stmt.length, resultPtr);

    if (ret & PYEXEC_EXCEPTION) {
        const exception_line = mod.HEAP32[(resultPtr + 4) >> 2];
        const filename_ptr = resultPtr + 8;
        console.error(`Exception at line ${exception_line}`);
    }
}
```

**Benefits**:
- ✅ Proper exception handling
- ✅ Line number tracking
- ✅ Integration with atexit, watchdog, etc.
- ✅ Supervisor hooks already in place

---

### Phase 3: Leverage `readline` for REPL Integration

Instead of reimplementing loop iteration, **use CircuitPython's existing REPL infrastructure**:

```c
// The REPL is event-driven and non-blocking!
int pyexec_event_repl_process_char(int c);
```

**Insight**: The REPL already knows how to:
- Process Python line-by-line
- Handle indentation
- Detect complete statements
- Execute incrementally

**For the supervisor, we could**:
1. Feed code to REPL character-by-character
2. Let REPL detect statement boundaries
3. Execute complete statements
4. Handle loops naturally

---

## Comparison: Old vs. New Approach

### Old Approach (JavaScript Parsing)
```javascript
// supervisor.js
parseCode(code) {
    const lines = code.split('\n');

    // Regex to find while True:
    if (line.match(/^\s*while\s+(True|1|true)\s*:/)) {
        // Extract loop body
        const loopBody = this.extractLoopBody(lines, loopStartIdx);
        // Manually dedent
        const dedentedLine = line.substring(baseIndent);
    }
}
```

**Problems**:
- ❌ Regex can't handle all Python syntax
- ❌ Manual dedenting duplicates lexer_dedent
- ❌ Doesn't detect `async def` reliably
- ❌ No line number tracking
- ❌ Reinvents the wheel

### New Approach (Using Existing Infrastructure)
```c
// C code
EMSCRIPTEN_KEEPALIVE
code_structure_t* analyze_code_structure(const char *code, size_t len) {
    mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(/* ... */);

    while (lex->tok_kind != MP_TOKEN_END) {
        if (lex->tok_kind == MP_TOKEN_KW_WHILE) {
            // Proper token-based detection
        }
        mp_lexer_to_next(lex);
    }
}
```

**Benefits**:
- ✅ Uses real Python lexer
- ✅ Automatic dedenting via lexer_dedent
- ✅ Detects all Python constructs correctly
- ✅ Line numbers built-in
- ✅ Leverages existing, tested code

---

## Implementation Plan

### Step 1: Export Lexer Functions
**File**: `ports/wasm/supervisor/code_analysis.c` (new)

```c
EMSCRIPTEN_KEEPALIVE code_structure_t* analyze_code_structure(...);
EMSCRIPTEN_KEEPALIVE mp_lexer_t* create_lexer(const char *code, size_t len);
EMSCRIPTEN_KEEPALIVE int get_next_token(mp_lexer_t *lex);
```

### Step 2: Update Supervisor.js to Use Lexer
**File**: `supervisor.js`

Replace regex parsing with:
```javascript
const structure = this.analyzeCodeStructure(code);
```

### Step 3: Use pyexec for Execution
**File**: `ports/wasm/supervisor/port.c`

```c
EMSCRIPTEN_KEEPALIVE
int supervisor_exec_code(const char *code, size_t len, pyexec_result_t *result);
```

### Step 4: Integrate Background Callbacks (from Phase 1)
Already designed - just add to the new infrastructure.

---

## Key Takeaway

**Don't reinvent CircuitPython in JavaScript!**

CircuitPython already has:
- ✅ `lexer` - tokenizes Python
- ✅ `lexer_dedent` - handles indentation
- ✅ `pyexec` - executes code properly
- ✅ `readline` - REPL infrastructure
- ✅ `parse` - AST generation
- ✅ `runtime` - execution engine

**The co-supervisor should**:
1. **Expose** these tools to JavaScript
2. **Drive** the execution loop from JavaScript
3. **Leverage** CircuitPython's existing infrastructure

Instead of:
```
JavaScript regex → Parse Python → Execute
```

Do:
```
JavaScript → Call C lexer → Get structure → Use pyexec
```

This gives us a **true CircuitPython port** that deeply integrates with the supervisor, not a JavaScript workaround bolted on top!

---

## Next Steps

1. **Create `code_analysis.c`** - Export lexer functions
2. **Update `supervisor.js`** - Remove regex, use lexer
3. **Export `pyexec` functions** - Proper execution
4. **Test** - Verify better error messages, line numbers, async support

The proof-of-concept supervisor works, but **this approach makes it production-ready** by using CircuitPython's battle-tested infrastructure instead of reinventing it.
