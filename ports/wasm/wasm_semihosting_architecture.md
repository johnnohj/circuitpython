# WASM Semihosting: The Perfect Co-Supervisor Pattern

## What is Semihosting?

**From [semihosting_rv32.h](semihosting_rv32.h:30-40)**:
> "To integrate semihosting, make sure to call mp_semihosting_init() first. Then, if the host system's STDOUT should be used instead of a UART, replace mp_hal_stdin_rx_chr and similar calls in mphalport.c with the semihosting equivalents."

**Semihosting** is a mechanism where an embedded system (RISC-V MCU in a debugger) makes "syscalls" to the host (the debugger/PC):
- Target asks host to read/write files
- Target asks host for time/clock info
- Target prints to host console
- Target executes host commands

**WASM is the same!**
- WASM = embedded target in browser
- JavaScript = the host system
- We need syscalls from WASM to JavaScript!

This is the **perfect architecture** for the co-supervisor!

---

## Mapping Semihosting to WASM

### 1. Console I/O → JavaScript Console

**Semihosting pattern** ([semihosting_rv32.h:106-115](semihosting_rv32.h:106-115)):
```c
int mp_semihosting_rx_char();         // Read from host STDIN
int mp_semihosting_tx_strn(...);      // Write to host STDOUT
int mp_semihosting_tx_strn_cooked(...); // Write with CR/LF conversion
```

**WASM equivalent**:
```c
// ports/wasm/wasm_semihosting.h

// Write Python print() output to JavaScript console
int mp_wasm_tx_strn(const char *str, size_t len);

// Read from JavaScript (terminal emulator, prompt(), etc.)
int mp_wasm_rx_char();
```

**JavaScript implementation**:
```javascript
// supervisor.js
class WASMSemihosting {
    tx_strn(str) {
        console.log(str);  // Python print() → console.log()
        this.terminalOutput?.append(str);  // Optional: terminal UI
    }

    rx_char() {
        // Read from terminal, prompt(), etc.
        return this.inputQueue.shift() || -1;
    }
}
```

---

### 2. Time/Clock → JavaScript Time

**Semihosting pattern** ([semihosting_rv32.h:198-203](semihosting_rv32.h:198-203)):
```c
int mp_semihosting_clock(void);  // Hundredths of second since start
int mp_semihosting_time(void);   // Unix timestamp
```

**WASM equivalent**:
```c
// ports/wasm/wasm_semihosting.h

// Get time from JavaScript (already implemented via virtual_clock_hw!)
uint64_t mp_wasm_get_ticks_ms();
uint64_t mp_wasm_get_unix_time();
```

**Already implemented!**
- `virtual_clock_hw.ticks_32khz` - JavaScript writes, WASM reads
- `emscripten_get_now()` - JavaScript time

This validates our virtual_clock design!

---

### 3. File System → JavaScript File API

**Semihosting pattern** ([semihosting_rv32.h:123-195](semihosting_rv32.h:123-195)):
```c
int mp_semihosting_open(const char *file_name, const char *file_mode);
int mp_semihosting_close(int handle);
int mp_semihosting_write(int handle, const void *data, size_t length);
int mp_semihosting_read(int handle, void *data, size_t length);
int mp_semihosting_remove(const char *file_name);
int mp_semihosting_rename(const char *old_name, const char *new_name);
```

**WASM equivalent**:
```c
// ports/wasm/wasm_semihosting.h

// Delegate filesystem to JavaScript
int mp_wasm_file_open(const char *path, const char *mode);
int mp_wasm_file_close(int handle);
int mp_wasm_file_read(int handle, void *buf, size_t len);
int mp_wasm_file_write(int handle, const void *buf, size_t len);
```

**JavaScript implementation**:
```javascript
// supervisor.js
class WASMSemihosting {
    async fileOpen(path, mode) {
        // IndexedDB, File API, or virtual filesystem
        const file = await this.fs.open(path, mode);
        return this.allocateHandle(file);
    }

    async fileRead(handle, length) {
        const file = this.handles.get(handle);
        return await file.read(length);
    }
}
```

This is cleaner than the VFS approach we tried before!

---

### 4. System Commands → JavaScript Execution

**Semihosting pattern** ([semihosting_rv32.h:207](semihosting_rv32.h:207)):
```c
// Execute command on host system
int mp_semihosting_system(const char *command);
```

**WASM equivalent**:
```c
// ports/wasm/wasm_semihosting.h

// Execute JavaScript code from Python!
int mp_wasm_eval_js(const char *code);
```

**JavaScript implementation**:
```javascript
// supervisor.js
class WASMSemihosting {
    evalJS(code) {
        try {
            return eval(code);  // Python executes JavaScript!
        } catch (e) {
            return -1;
        }
    }
}
```

**Python usage**:
```python
# From Python, execute JavaScript!
import jsffi
result = jsffi.eval("document.body.style.backgroundColor = 'red'")
```

---

### 5. Heap Info → Browser Memory

**Semihosting pattern** ([semihosting_rv32.h:222-224](semihosting_rv32.h:222-224)):
```c
typedef struct {
    void *heap_base;
    void *heap_limit;
    void *stack_base;
    void *stack_limit;
} mp_semihosting_heap_info_t;

void mp_semihosting_heapinfo(mp_semihosting_heap_info_t *block);
```

**WASM equivalent**:
```c
// Query browser memory info
typedef struct {
    size_t total_js_heap_size;
    size_t used_js_heap_size;
    size_t wasm_memory_size;
} mp_wasm_memory_info_t;

void mp_wasm_get_memory_info(mp_wasm_memory_info_t *info);
```

**JavaScript implementation**:
```javascript
getMemoryInfo() {
    return {
        totalHeapSize: performance.memory?.totalJSHeapSize || 0,
        usedHeapSize: performance.memory?.usedJSHeapSize || 0,
        wasmMemorySize: Module.HEAPU8.length
    };
}
```

---

### 6. Supervisor Control → JavaScript Callbacks

**Semihosting pattern** ([semihosting_rv32.h:116-119](semihosting_rv32.h:116-119)):
```c
// Terminate execution
noreturn void mp_semihosting_terminate(uint32_t code, uint32_t subcode);
```

**WASM equivalent**:
```c
// Supervisor control from Python
void mp_wasm_supervisor_reload();
void mp_wasm_supervisor_pause();
void mp_wasm_supervisor_resume();
void mp_wasm_supervisor_stop(uint32_t exit_code);
```

**JavaScript implementation**:
```javascript
class WASMSemihosting {
    supervisorReload() {
        this.supervisor.reload();
    }

    supervisorStop(exitCode) {
        this.supervisor.stop();
        if (exitCode !== 0) {
            console.error(`Execution stopped with code ${exitCode}`);
        }
    }
}
```

**Python usage**:
```python
import supervisor
supervisor.reload()  # → JavaScript supervisor.reload()
```

---

## Implementation: wasm_semihosting.h

```c
// ports/wasm/wasm_semihosting.h
// Semihosting interface for WASM to call JavaScript "host"

#ifndef MICROPY_INCLUDED_WASM_SEMIHOSTING_H
#define MICROPY_INCLUDED_WASM_SEMIHOSTING_H

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Console I/O - JavaScript console
// ============================================================================

// Write to JavaScript console.log() / terminal
int mp_wasm_tx_strn(const char *str, size_t len);

// Write with CR/LF conversion (for REPL)
int mp_wasm_tx_strn_cooked(const char *str, size_t len);

// Read from JavaScript input (terminal, prompt, etc.)
int mp_wasm_rx_char();

// ============================================================================
// Time - JavaScript time
// ============================================================================

// Get milliseconds since page load
uint64_t mp_wasm_get_ticks_ms();

// Get Unix timestamp (milliseconds since epoch)
uint64_t mp_wasm_get_unix_time();

// ============================================================================
// File System - JavaScript File API / IndexedDB
// ============================================================================

int mp_wasm_file_open(const char *path, const char *mode);
int mp_wasm_file_close(int handle);
int mp_wasm_file_read(int handle, void *buf, size_t len);
int mp_wasm_file_write(int handle, const void *buf, size_t len);
int mp_wasm_file_seek(int handle, int offset, int whence);
int mp_wasm_file_remove(const char *path);
int mp_wasm_file_rename(const char *old_path, const char *new_path);

// ============================================================================
// JavaScript Execution - Run JS from Python
// ============================================================================

// Execute JavaScript code and return result
int mp_wasm_eval_js(const char *code, char *result_buf, size_t buf_len);

// ============================================================================
// Supervisor Control - Control from Python
// ============================================================================

void mp_wasm_supervisor_reload();
void mp_wasm_supervisor_pause();
void mp_wasm_supervisor_resume();
void mp_wasm_supervisor_stop(uint32_t exit_code);

// ============================================================================
// Memory Info - Browser memory stats
// ============================================================================

typedef struct {
    size_t total_js_heap_size;
    size_t used_js_heap_size;
    size_t wasm_memory_size;
} mp_wasm_memory_info_t;

void mp_wasm_get_memory_info(mp_wasm_memory_info_t *info);

// ============================================================================
// Initialization
// ============================================================================

void mp_wasm_semihosting_init();

#endif // MICROPY_INCLUDED_WASM_SEMIHOSTING_H
```

---

## Integration Pattern

### Step 1: Initialize Semihosting
```c
// ports/wasm/supervisor/port.c

#include "wasm_semihosting.h"

safe_mode_t port_init(void) {
    // Initialize semihosting FIRST
    mp_wasm_semihosting_init();

    // Now CircuitPython can use JavaScript "syscalls"
    enable_all_pins();
    reset_port();

    return SAFE_MODE_NONE;
}
```

### Step 2: Replace HAL Functions
```c
// ports/wasm/mphalport.c

#include "wasm_semihosting.h"

// Redirect stdin to JavaScript
int mp_hal_stdin_rx_chr(void) {
    return mp_wasm_rx_char();  // Semihosting!
}

// Redirect stdout to JavaScript
void mp_hal_stdout_tx_strn(const char *str, size_t len) {
    mp_wasm_tx_strn(str, len);  // Semihosting!
}
```

### Step 3: Implement in JavaScript
```javascript
// supervisor.js

class WASMSemihosting {
    constructor(wasmModule) {
        this.wasm = wasmModule;
        this.inputQueue = [];
        this.fileHandles = new Map();
        this.nextHandle = 1;

        // Register semihosting functions
        this.registerCallbacks();
    }

    registerCallbacks() {
        const mod = this.wasm._module;

        // Console I/O
        mod._mp_wasm_tx_strn_impl = (strPtr, len) => {
            const str = mod.UTF8ToString(strPtr, len);
            console.log(str);
            return 0;
        };

        mod._mp_wasm_rx_char_impl = () => {
            return this.inputQueue.shift() || -1;
        };

        // Time
        mod._mp_wasm_get_ticks_ms_impl = () => {
            return BigInt(Date.now());
        };

        // File system
        mod._mp_wasm_file_open_impl = (pathPtr, modePtr) => {
            const path = mod.UTF8ToString(pathPtr);
            const mode = mod.UTF8ToString(modePtr);
            return this.fileOpen(path, mode);
        };

        // Supervisor control
        mod._mp_wasm_supervisor_reload_impl = () => {
            this.supervisor.reload();
        };
    }

    fileOpen(path, mode) {
        // IndexedDB, File API, etc.
        const handle = this.nextHandle++;
        this.fileHandles.set(handle, { path, mode, pos: 0 });
        return handle;
    }
}
```

---

## Benefits of Semihosting Pattern

### ✅ Clean Separation
- C code doesn't know about JavaScript
- JavaScript implements "host services"
- Well-defined interface

### ✅ Standard Pattern
- Used by RISC-V, ARM, etc.
- Proven architecture
- CircuitPython already has experience with it

### ✅ Extensible
- Add new syscalls easily
- JavaScript can provide rich implementations
- Python gets access to browser APIs

### ✅ Matches Co-Supervisor Model
- WASM "calls out" to JavaScript
- JavaScript supervisor provides services
- Clean bidirectional communication

---

## Comparison to Current Approach

### Current: Ad-hoc FFI
```c
// Scattered throughout code
EM_JS(void, console_log, (const char *str), {
    console.log(UTF8ToString(str));
});

// No organization
// Hard to maintain
// Duplicated code
```

### With Semihosting: Clean Interface
```c
// Well-defined header
#include "wasm_semihosting.h"

// Organized syscalls
mp_wasm_tx_strn(str, len);
mp_wasm_supervisor_reload();

// Single place for all host calls
// Easy to mock for testing
// Clear API contract
```

---

## Next Steps

1. **Create wasm_semihosting.h** - Define interface
2. **Implement in C** - EM_JS implementations
3. **Create JavaScript host** - WASMSemihosting class
4. **Replace HAL** - Use semihosting in mphalport.c
5. **Extend** - Add more syscalls as needed

This transforms the WASM port from "JavaScript hacks" into a **proper semihosted embedded system** following established patterns!
