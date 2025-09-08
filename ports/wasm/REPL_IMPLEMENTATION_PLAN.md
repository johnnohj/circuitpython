# CircuitPython WASM REPL Implementation Plan

## Current Status

**SUCCESS**: We have successfully built CircuitPython WebAssembly with HAL Layer 2 architecture!

✅ **Working Components:**
- Complete WASM build system generating proper ES6 modules (.mjs + .wasm)
- HAL provider framework with JavaScript hardware abstraction
- Character-by-character REPL processing framework  
- Web editor integration with xterm.js terminal
- Memory allocation and module initialization
- CircuitPython API structure (digitalio, board modules built)

❌ **Missing for Functional REPL:**
- Actual Python code execution (currently stub implementations)
- Real MicroPython REPL state machine
- Output routing from Python print() statements

## Problem Analysis

Our build creates the complete HAL Layer 2 architecture but lacks the core MicroPython runtime components needed for actual Python code execution:

- `mp_js_repl_init()` and `mp_js_repl_process_char()` are stubs
- Missing `pyexec_event_repl_init()` and `pyexec_event_repl_process_char()`
- No connection between Python print() and JavaScript output callbacks

## Implementation Strategy

### Phase 1: MicroPython REPL Integration 
**Priority: CRITICAL**

Replace stub implementations with real MicroPython REPL functions:

**File: `/ports/wasm/main.c`**
```c
// Add missing include
#include "shared/runtime/pyexec.h"

// Replace current stub implementations:
void mp_js_repl_init(void) {
    pyexec_event_repl_init();  // Real MicroPython REPL init
}

int mp_js_repl_process_char(int c) {
    external_call_depth_inc();
    int ret = pyexec_event_repl_process_char(c);  // Real character processing
    external_call_depth_dec();
    return ret;
}

// Remove the stub implementations in the #ifndef block
```

**File: `/ports/wasm/Makefile`**
```makefile
# Add missing shared runtime sources
SRC_SHARED_MODULE = \
	shared/runtime/pyexec.c \
	shared/runtime/interrupt_char.c \

# Ensure these are included in build
SRC_C += $(SRC_SHARED_MODULE)
```

**File: `/ports/wasm/mpconfigport.h`**
```c
// Ensure REPL features are enabled
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_REPL_EVENT_DRIVEN (1)
#define MICROPY_ENABLE_COMPILER (1)
```

### Phase 2: Python Execution Engine
**Priority: HIGH**

Enable actual Python code compilation and execution:

1. **Verify MicroPython core inclusion**:
   - Ensure `py/compile.c`, `py/runtime.c`, `py/vm.c` are building
   - Add missing builtins: `mp_builtin_print`, `mp_builtin_help`

2. **Fix stdout/stderr routing**:
```c
// In mphalport.c - fix the signature mismatch
void mp_hal_stdout_tx_strn(const char *str, size_t len) {
    // Route to JavaScript callback for terminal display
    EM_ASM({
        if (Module.outputCallback) {
            var text = UTF8ToString($0, $1);
            Module.outputCallback(text);
        }
    }, str, len);
}
```

3. **Add execution context setup**:
```c
// Ensure proper module initialization
void mp_js_init_with_heap(int heap_size) {
    // ... existing initialization ...
    
    // Initialize module system
    mp_obj_list_init(mp_sys_path, 0);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    
    // Register built-in modules
    mp_module_register(MP_QSTR_digitalio, (mp_obj_t)&digitalio_module);
}
```

### Phase 3: CircuitPython Module Integration
**Priority: MEDIUM**

Make digitalio and HAL modules fully functional:

**File: `/ports/wasm/digitalio_module.c`**
- Restore constructor and method implementations (currently simplified)
- Connect DigitalInOut class to HAL provider system
- Add proper error handling and type checking

**Add board module**:
```c
// Create board.c with pin definitions using HAL providers
const mp_obj_module_t mp_module_board = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&board_module_globals,
};
```

### Phase 4: Memory and Runtime Fixes  
**Priority: LOW**

Complete runtime initialization:

1. **Fix gc_collect() implementation** (currently basic stub)
2. **Add proper exception handling**
3. **Implement soft reset functionality**

## Build Configuration Success

Our current Makefile configuration successfully generates:
- `circuitpython.mjs` (68KB) - ES6 module with proper exports
- `circuitpython.wasm` (133KB) - WebAssembly binary with all symbols
- All HAL provider functions exported and accessible
- Clean integration with web editor's virtual workflow

## Web Editor Integration Status

✅ **Complete:**
- `circuitpython-wasm-worker-hal.js` - New HAL-enabled worker  
- `virtual.js` updated to use HAL worker
- Character-by-character processing integrated with xterm.js
- Styling and terminal setup preserved

## Immediate Next Steps

1. **Implement Phase 1** (REPL integration) - Should provide immediate Python execution
2. **Test basic commands**: `print("hello")`, `2+3`, `import digitalio`
3. **Fix output routing** if print statements don't display
4. **Add CircuitPython modules** once basic REPL works

## Expected Outcome

After Phase 1 implementation:
```
>>> print("Hello CircuitPython HAL!")
Hello CircuitPython HAL!
>>> 2 + 3
5
>>> import digitalio
>>> help()
Welcome to CircuitPython!
```

## Architecture Achievement

This implementation completes your **refactored /bin/standard build extending bin/core with HAL Layer 2 design**, providing:

- ✅ **Straightforward Node.js REPL integration** (solved previous challenges)
- ✅ **HAL abstraction layer** for JavaScript hardware providers
- ✅ **CircuitPython educational API** with familiar modules
- ✅ **WebAssembly performance** with native code execution
- ✅ **Clean web editor integration** with existing xterm.js terminal

## Success Metrics

- [x] WebAssembly builds without errors
- [x] ES6 module exports all required functions  
- [x] HAL provider system architecture complete
- [x] Web editor virtual workflow integration complete
- [ ] **Next**: Actual Python code execution in REPL ⬅️ **Current blocker**

---

*Plan created: 2025-01-07*  
*Build status: Successful HAL Layer 2 architecture*  
*Next phase: Python execution integration*