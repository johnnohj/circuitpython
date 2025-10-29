# CircuitPython WASM Co-Supervisor Architecture

## Overview

Instead of having JavaScript and C supervisors operate independently, we create a **co-supervisor** architecture where JavaScript and C work together, each handling what they do best.

## The Problem We're Solving

Current state:
- **C Supervisor**: Expects to run continuously in a traditional embedded loop
- **JavaScript Supervisor** (our proof-of-concept): Parses Python code, drives iterations via `requestAnimationFrame()`
- **Disconnect**: They don't communicate or share state properly

Desired state:
- **Tight integration** between C background tasks and JavaScript event loop
- **Proper async/await support** using CircuitPython's existing infrastructure
- **Background callbacks** exposed as JavaScript events
- **Unified timing** via the existing `virtual_clock_hw`

---

## Key Integration Points

### 1. Background Callbacks as JavaScript Events

**C Side** ([background_callback.h](background_callback.h)):
```c
typedef void (*background_callback_fun)(void *data);
typedef struct background_callback {
    background_callback_fun fun;
    void *data;
    struct background_callback *next;
    struct background_callback *prev;
} background_callback_t;

void background_callback_add(background_callback_t *cb, background_callback_fun fun, void *data);
void background_callback_run_all(void);
bool background_callback_pending(void);
```

**Bridge to JavaScript**:
```c
// In ports/wasm/supervisor/port.c

// Export for JavaScript to poll
EMSCRIPTEN_KEEPALIVE
bool supervisor_has_pending_callbacks(void) {
    return background_callback_pending();
}

// Export for JavaScript to trigger
EMSCRIPTEN_KEEPALIVE
void supervisor_run_callbacks(void) {
    background_callback_run_all();
}
```

**JavaScript Side** (enhanced supervisor.js):
```javascript
class CircuitPythonSupervisor {
    runIteration() {
        // BEFORE executing Python code
        if (this.wasm._supervisor_has_pending_callbacks?.()) {
            this.wasm._supervisor_run_callbacks();
        }

        // Execute Python code...
        this.executeWithSleep(this.loopCode, () => {
            this.scheduleNextIteration();
        });
    }
}
```

### 2. Tick System Integration

**Current WASM Implementation** ([port.c:180-194](port.c:180-194)):
```c
uint64_t port_get_raw_ticks(uint8_t *subticks) {
    uint64_t ticks_32khz = read_virtual_ticks_32khz();
    uint64_t ticks_1024hz = ticks_32khz / 32;
    if (subticks != NULL) {
        *subticks = (uint8_t)(ticks_32khz % 32);
    }
    return ticks_1024hz;
}
```

**JavaScript Drives Time** (existing virtual_clock.js pattern):
```javascript
class VirtualClock {
    update(deltaMs) {
        // Write directly to shared memory
        const ticks = Math.floor(deltaMs * 32.768); // Convert ms to 32kHz
        this.view.setBigUint64(0, BigInt(ticks), true);
    }
}
```

**Integration**:
- JavaScript's `requestAnimationFrame()` advances virtual time
- C supervisor reads time instantly from shared memory
- No async calls needed - just memory reads!

### 3. RUN_BACKGROUND_TASKS Macro

**Current CircuitPython Pattern**:
```c
// Scattered throughout CircuitPython code
RUN_BACKGROUND_TASKS;  // Expands to background_callback_run_all()
```

**WASM Enhancement**:
```c
// In mpconfigport.h
#define RUN_BACKGROUND_TASKS \
    do { \
        background_callback_run_all(); \
        port_background_task(); \  // Already yields to JS
    } while(0)
```

This means **CircuitPython's existing code** already yields to JavaScript at appropriate points!

### 4. Python-Assisted Code Parsing

Instead of JavaScript regex parsing, **use Python's AST module**:

**New C Function**:
```c
// ports/wasm/supervisor/code_parser.c

EMSCRIPTEN_KEEPALIVE
mp_obj_t supervisor_parse_code_structure(const char *code) {
    // Use Python's ast module to parse code structure
    mp_obj_t ast_module = mp_import_name(MP_QSTR_ast, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t parse_fn = mp_load_attr(ast_module, MP_QSTR_parse);

    // Parse code
    mp_obj_t ast_tree = mp_call_function_1(parse_fn, mp_obj_new_str(code, strlen(code)));

    // Walk AST to find:
    // - Setup code (everything before first While)
    // - While True loops
    // - async def functions
    // - asyncio.run() calls

    return parsed_structure;
}
```

**JavaScript Calls Python**:
```javascript
const structure = this.wasm._supervisor_parse_code_structure(codePtr);
// Returns: { setup, loops: [...], asyncFunctions: [...] }
```

**Benefits**:
- Handles complex Python syntax correctly
- Supports asyncio patterns natively
- Can detect `async def`, `await`, etc.

---

## Architecture: The Yield Point

**Key Insight from [port.c:216-228](port.c:216-228)**:

```c
void port_idle_until_interrupt(void) {
    // This is where WASM yields to JavaScript!
    common_hal_mcu_disable_interrupts();
    if (!background_callback_pending() && !_woken_up) {
        // Yield point - JavaScript event loop runs here
        // In real hardware this would be __WFI() (wait for interrupt)
    }
    common_hal_mcu_enable_interrupts();
}
```

This is called from:
- `time.sleep()` → `mp_hal_delay_ms()` → `port_idle_until_interrupt()`
- Waiting for I/O
- REPL waiting for input

**Emscripten Implementation**:
```c
void port_idle_until_interrupt(void) {
    // Emscripten_sleep() returns control to JavaScript!
    // But we don't actually want to block...

    // Instead, mark that we're yielding
    emscripten_sleep(0);  // Immediate yield to JS event loop
}
```

---

## Co-Supervisor Data Flow

```
┌─────────────────────────────────────────────────────────────┐
│                     JavaScript Event Loop                    │
│  (requestAnimationFrame, setTimeout, user interactions)      │
└───────────────┬─────────────────────────────────────────────┘
                │
                ▼
┌───────────────────────────────────────────────────────────────┐
│           JavaScript Supervisor (supervisor.js)                │
│  • Parses code structure (via Python AST)                     │
│  • Manages while True loop iterations                         │
│  • Handles time.sleep() as setTimeout                         │
│  • Advances virtual_clock_hw.ticks_32khz                      │
│  • Checks for pending background callbacks                    │
└───────────────┬───────────────────────────────────────────────┘
                │
                │ runPython(code)
                ▼
┌───────────────────────────────────────────────────────────────┐
│         Python VM (MicroPython/CircuitPython)                 │
│  • Executes user code                                         │
│  • Calls RUN_BACKGROUND_TASKS at strategic points             │
│  • Yields at port_idle_until_interrupt()                      │
└───────────────┬───────────────────────────────────────────────┘
                │
                │ RUN_BACKGROUND_TASKS
                ▼
┌───────────────────────────────────────────────────────────────┐
│           C Supervisor (supervisor/*.c)                       │
│  • background_callback_run_all()                              │
│  • supervisor_tick() (if enabled)                             │
│  • port_background_tick()                                     │
│  • port_background_task()                                     │
└───────────────┬───────────────────────────────────────────────┘
                │
                │ Reads timing
                ▼
┌───────────────────────────────────────────────────────────────┐
│        Shared Memory (virtual_clock_hw, hardware states)      │
│  • virtual_clock_hw.ticks_32khz (written by JS, read by C)    │
│  • gpio_states[] (written by C, read by JS)                   │
│  • encoder_states[] (written by C, read by JS)                │
└───────────────────────────────────────────────────────────────┘
```

---

## Implementation Phases

### Phase 1: Background Callback Bridge ✨
**Goal**: Expose C background callbacks to JavaScript

Files to modify:
- `ports/wasm/supervisor/port.c` - Add exported functions
- `supervisor.js` - Check and run callbacks each iteration

```c
// Add to port.c
EMSCRIPTEN_KEEPALIVE
bool supervisor_has_pending_callbacks(void) {
    return background_callback_pending();
}

EMSCRIPTEN_KEEPALIVE
void supervisor_run_callbacks(void) {
    background_callback_run_all();
}
```

```javascript
// Enhance supervisor.js
runIteration() {
    const mod = this.wasm._module;

    // Run any pending C callbacks
    if (mod._supervisor_has_pending_callbacks?.()) {
        mod._supervisor_run_callbacks();
    }

    // Continue with Python execution
    this.executeWithSleep(this.loopCode, () => {
        this.scheduleNextIteration();
    });
}
```

### Phase 2: Python AST Parser 🎯
**Goal**: Use Python's AST module to parse code instead of regex

Files to create:
- `ports/wasm/supervisor/code_parser.c` - Python-based parser

```c
// New file: ports/wasm/supervisor/code_parser.c
EMSCRIPTEN_KEEPALIVE
mp_obj_t supervisor_parse_code_structure(const char *code, size_t len) {
    // Import ast module
    mp_obj_t ast = mp_import_name(qstr_from_str("ast"), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));

    // Parse: ast.parse(code)
    mp_obj_t code_str = mp_obj_new_str(code, len);
    mp_obj_t tree = mp_call_function_1(
        mp_load_attr(ast, qstr_from_str("parse")),
        code_str
    );

    // Walk tree to find structure
    return analyze_ast_tree(tree);
}
```

### Phase 3: Asyncio Integration 🚀
**Goal**: Support `async def` and `asyncio.run()` patterns

**Detection**:
```python
# User code
import asyncio

async def main():
    while True:
        print("Async loop!")
        await asyncio.sleep(1)

asyncio.run(main())
```

**JavaScript Supervisor**:
```javascript
if (structure.hasAsyncio) {
    // Use runPythonAsync instead of runPython
    await this.wasm.runPythonAsync(this.setupCode);
    // Python handles its own event loop via asyncio
}
```

### Phase 4: Reload/Autoreload Support 🔄
**Goal**: Integrate with CircuitPython's autoreload system

```c
// Export from supervisor/shared/reload.c
EMSCRIPTEN_KEEPALIVE
void supervisor_reload_initiate(uint8_t reason) {
    reload_initiate((supervisor_run_reason_t)reason);
}

EMSCRIPTEN_KEEPALIVE
bool supervisor_autoreload_pending(void) {
    return autoreload_pending();
}
```

```javascript
// JavaScript file watcher
fs.onFileChange('code.py', () => {
    mod._supervisor_reload_initiate(RELOAD_REASON_AUTO);
});
```

---

## Benefits of Co-Supervisor Architecture

### ✅ Leverages Existing CircuitPython Infrastructure
- No need to reimplement background callbacks
- Proper tick system integration
- Autoreload works as expected
- Safe mode handling

### ✅ Non-Blocking by Design
- JavaScript drives timing
- `setTimeout` for sleep
- `requestAnimationFrame` for iterations
- Browser stays responsive

### ✅ Asyncio Support
- Python's async/await works natively
- CircuitPython's asyncio module functional
- Proper coroutine scheduling

### ✅ Better Code Parsing
- Python AST instead of regex
- Handles complex syntax
- Detects async patterns
- Can provide better error messages

### ✅ Hardware Simulation
- Background callbacks can trigger from "hardware events"
- JavaScript can schedule callbacks (e.g., UART data received)
- True interrupt simulation

---

## Example: Simulated Hardware Interrupt

**Scenario**: UART receives data from JavaScript

```javascript
// JavaScript: User types in terminal
terminal.onData((data) => {
    // Write to UART buffer (shared memory)
    const uartState = new DataView(mod.HEAPU8.buffer, mod._get_uart_state_ptr());
    // ... write data to buffer ...

    // Queue a background callback in C!
    mod._uart_trigger_rx_callback(0); // UART index 0
});
```

**C Side**:
```c
// common-hal/busio/UART.c

EMSCRIPTEN_KEEPALIVE
void uart_trigger_rx_callback(uint8_t uart_index) {
    uart_state_t *state = &uart_states[uart_index];
    if (state->rx_callback.fun != NULL) {
        // Add to background callback queue
        background_callback_add_core(&state->rx_callback);
    }
}
```

**Result**: JavaScript hardware event → C background callback → Python interrupt handler!

---

## Next Steps

1. **Implement Phase 1** (Background Callback Bridge)
   - Minimal changes to existing code
   - Immediate benefit: proper async task handling

2. **Test with rotaryio**
   - rotaryio already uses background callbacks for state updates
   - Perfect test case for the co-supervisor

3. **Add Python AST Parser** (Phase 2)
   - More robust than regex
   - Foundation for asyncio support

4. **Document the Architecture**
   - Write WASM port guide
   - Explain co-supervisor concept
   - Help future contributors

---

## Summary

The **Co-Supervisor** architecture transforms the WASM port from "CircuitPython APIs bolted onto MicroPython" into a true CircuitPython implementation that:

1. **Respects CircuitPython's supervisor design**
2. **Leverages JavaScript's event loop naturally**
3. **Provides proper async/await support**
4. **Enables realistic hardware simulation**
5. **Maintains non-blocking browser execution**

The key insight: **Don't fight the VM - embrace it**. JavaScript doesn't replace the C supervisor; it **drives** it through carefully designed integration points.
