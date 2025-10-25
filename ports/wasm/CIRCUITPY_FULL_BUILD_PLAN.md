# WASM Port: CIRCUITPY_FULL_BUILD Implementation Plan

## Understanding CIRCUITPY_FULL_BUILD

CIRCUITPY_FULL_BUILD is a feature flag that controls:
- **Python language features** (complex numbers, string methods, etc.)
- **Module availability** (more built-in modules)
- **Error reporting** (verbose vs terse)

It does NOT control filesystem - that's separate.

## What We Need to Provide for Full CircuitPython

### 1. Supervisor Functions (Required by CircuitPython Core)

From `supervisor/supervisor.mk`, a full CircuitPython build expects:

**Core Runtime:**
```c
supervisor/shared/micropython.c     // ⚠️ VM lifecycle management
supervisor/shared/reload.c          // ⚠️ Auto-reload on file change
supervisor/shared/safe_mode.c       // ⚠️ Error handling
```

**I/O & Storage:**
```c
supervisor/shared/filesystem.c      // ⚠️ VFS management
supervisor/shared/serial.c          // ✅ We have this
supervisor/shared/display.c         // ❓ Terminal emulation
```

**Background Processing:**
```c
supervisor/shared/background_callback.c  // ✅ We have this
supervisor/shared/tick.c                 // ✅ We have this
```

**Translation:**
```c
supervisor/shared/translate/translate.c  // ✅ We have this
```

### 2. Our Current Status

**✅ Have (5/11 core supervisor files):**
- background_callback.c
- tick.c
- translate/translate.c
- port.c (our implementation)
- serial.c

**⚠️ Missing (6/11 core supervisor files):**
- micropython.c - VM lifecycle
- reload.c - Auto-reload
- safe_mode.c - Error handling
- filesystem.c - VFS management
- display.c - Terminal display
- board.c - Board utilities

### 3. Strategy: Stub vs Implement

For each missing component, decide:
- **IMPLEMENT** - Provide full functionality (using JS/Emscripten)
- **ADAPT** - Modify CircuitPython's version for WASM
- **STUB** - Provide no-op functions (not applicable to WASM)

## Component-by-Component Analysis

### A. supervisor/shared/filesystem.c

**What it does on real hardware:**
- Mounts FAT filesystem on flash
- Manages CIRCUITPY USB drive
- Handles concurrent access (Python vs USB)
- Periodic flush to flash

**What WASM needs:**
```c
// supervisor/filesystem_wasm.c (NEW FILE)

#include "supervisor/filesystem.h"
#include <emscripten.h>

// JavaScript already manages filesystem via filesystem.js
// We just need to provide the API that CircuitPython expects

bool filesystem_init(bool create_allowed, bool force_create) {
    // JavaScript already mounted filesystem via syncToVFS()
    // Just verify /circuitpy or / is accessible
    return true;
}

void filesystem_flush(void) {
    // Tell JavaScript to sync VFS to IndexedDB
    EM_ASM({
        if (Module.filesystem && Module.filesystem.syncFromVFS) {
            // Sync all changed files
            const paths = FS.readdir('/').filter(f => f !== '.' && f !== '..').map(f => '/' + f);
            Module.filesystem.syncFromVFS(Module, paths).catch(err => {
                console.error('Filesystem flush failed:', err);
            });
        }
    });
}

void filesystem_background(void) {
    // Background flush - no-op, JavaScript handles async
}

void filesystem_tick(void) {
    // Periodic flush timer - no-op
}

bool filesystem_is_writable_by_python(fs_user_mount_t *vfs) {
    return true;  // Always writable in WASM
}

bool filesystem_is_writable_by_usb(fs_user_mount_t *vfs) {
    return false;  // No USB in WASM
}

void filesystem_set_writable_by_usb(fs_user_mount_t *vfs, bool writable) {
    // No-op
}

// Stub other filesystem.h functions...
```

**Status:** IMPLEMENT (simple wrapper around JavaScript)

---

### B. supervisor/shared/micropython.c

**What it does:**
- Runs boot.py
- Runs code.py or enters REPL
- Handles VM initialization

**What WASM needs:**
```c
// Already partially handled by api.js:
// - Checks for /boot.py and runs it
// - Checks for /code.py and runs it

// Option 1: Keep JavaScript-driven (current)
// Option 2: Move to C supervisor for standard CircuitPython behavior
```

**Status:** ADAPT (api.js already does this, could move to C)

---

### C. supervisor/shared/reload.c

**What it does:**
- Detects file changes
- Auto-reloads Python code
- Hot-reload feature

**What WASM needs:**
```c
// Could use JavaScript FileSystemObserver API
// Or periodic polling of file timestamps

void supervisor_reload(void) {
    // Trigger reload - call into JavaScript
    EM_ASM({
        if (Module.onReload) {
            Module.onReload();
        }
    });
}

bool supervisor_should_reload(void) {
    // Check if files changed
    // Could track mtime of /code.py
    return false;  // For now, manual reload only
}
```

**Status:** STUB initially, IMPLEMENT later (nice-to-have feature)

---

### D. supervisor/shared/safe_mode.c

**What it does:**
- Handles Python exceptions
- Displays error messages
- Boots into safe mode on crash

**What WASM needs:**
```c
// Most of safe_mode.c can be used as-is
// Just need to implement display output

void safe_mode_print_error(const char *message) {
    // Print to console
    EM_ASM({
        console.error(UTF8ToString($0));
    }, message);
}
```

**Status:** ADAPT (use CircuitPython's version, redirect output)

---

### E. supervisor/shared/display.c

**What it does:**
- Terminal emulation
- Status bar
- Title text on displays

**What WASM needs:**
```c
// Could render to HTML canvas
// Or just stub for console output

void supervisor_display_message(const char *message) {
    printf("%s\n", message);  // Goes to JavaScript console
}
```

**Status:** STUB initially (console output), IMPLEMENT later (canvas terminal)

---

### F. supervisor/shared/board.c

**What it does:**
- Board-specific utilities
- Pin initialization

**What WASM needs:**
```c
// Minimal implementation
void board_init(void) {
    // Already done in port_init()
}
```

**Status:** STUB (minimal functions)

---

## Implementation Priority

### Phase 1: Critical (Get filesystem working)
1. ✅ Create `supervisor/filesystem_wasm.c`
2. ✅ Add to Makefile
3. ✅ Wire up `filesystem_flush()` to JavaScript
4. Test: Files persist after flush

### Phase 2: Auto-run (Get boot.py/code.py working from C)
5. Add `supervisor/shared/micropython.c` OR keep api.js approach
6. Decision: JavaScript-driven vs C-driven?

### Phase 3: Error Handling
7. Add `supervisor/shared/safe_mode.c`
8. Redirect error output to console

### Phase 4: Auto-reload (Nice to have)
9. Add `supervisor/shared/reload.c`
10. Hook into JavaScript file watching

### Phase 5: Display (Optional)
11. Add `supervisor/shared/display.c` stub
12. Later: Implement canvas terminal

## Decision: Which Files to Add Now?

**Minimal set for basic CircuitPython compatibility:**
```makefile
SRC_SUPERVISOR = $(addprefix supervisor/shared/,\
	background_callback.c \   # ✅ Have
	tick.c \                  # ✅ Have
	translate/translate.c \   # ✅ Have
	board.c \                 # ⚠️ ADD (stub)
	safe_mode.c \             # ⚠️ ADD (adapt)
	)
SRC_SUPERVISOR += supervisor/port.c           # ✅ Have
SRC_SUPERVISOR += supervisor/serial.c         # ✅ Have
SRC_SUPERVISOR += supervisor/filesystem_wasm.c  # ⚠️ CREATE NEW
```

**Full set for CIRCUITPY_FULL_BUILD:**
Add the above plus:
```makefile
SRC_SUPERVISOR += supervisor/shared/micropython.c  # ⚠️ ADD or keep JS
SRC_SUPERVISOR += supervisor/shared/reload.c       # ⚠️ ADD (stub initially)
SRC_SUPERVISOR += supervisor/shared/display.c      # ⚠️ ADD (stub initially)
```

## Next Concrete Steps

1. **Create `supervisor/filesystem_wasm.c`** with stubs
2. **Add to Makefile**
3. **Test build**
4. **Implement `filesystem_flush()`** with JS integration
5. **Test persistence**

Want me to create the filesystem_wasm.c file?
