# Lifecycle States — JS ↔ C Contract

## States

```
LOADING  →  READY  →  RUNNING_CODE  →  SWITCHING  →  READY
                  →  RUNNING_REPL  →  SWITCHING  →  READY
```

### LOADING
- JS: instantiate WASM, create MEMFS, restore IDBFS, register hardware modules
- C: `cp_init()` — mp_init, GC, pystack, HAL fds, display framebuffer
- Exit: cp_init returns → READY

### READY (idle)
- VM is initialized but not executing user code
- Display shows supervisor terminal (Blinka + boot text)
- All pins unclaimed, no buses open
- Hardware modules at initial state (or restored from initialHardwareState.json)
- Editor is visible, user can edit code.py
- User picks: Run (→ RUNNING_CODE) or REPL (→ RUNNING_REPL)

### RUNNING_CODE
- code.py executing via cp_run / vm_yield_step
- Display shows program's displayio output (root_group set by user code)
- Pins may be claimed, buses open, NeoPixels lit
- Hardware modules running preStep/postStep each frame
- Exit: code finishes naturally, user clicks Stop, or user clicks REPL
  → SWITCHING

### RUNNING_REPL
- REPL active on display (supervisor terminal with >>> prompt)
- Keyboard input → readline → exec → display
- Pins/buses from REPL expressions may be active
- Exit: user clicks Run or Stop → SWITCHING

### SWITCHING
- Transitional state, not visible to user
- Sequence:
  1. Stop execution (vm_yield_stop, ctrlC if needed)
  2. Reset hardware to initial state:
     - reset_displays() — root_group back to supervisor terminal
     - reset_board_buses() — deinit I2C/SPI/UART singletons
     - reset_all_pins() — clear .claimed flags
     - JS: restore hardware module state from initialHardwareState
       (GPIO slots, NeoPixel data, analog values — all MEMFS endpoints)
  3. gc_collect() — free unreachable heap objects
  4. Clear serial output buffer
- Exit: → READY

## Who Owns What

### Layer 1: WASM Instance (JS owns, never destroyed)
- WebAssembly memory
- MEMFS (Map<string, Uint8Array>)
- IDBFS (IndexedDB persistence)
- Hardware modules (GpioModule, NeoPixelModule, etc.)
- Display renderer (display.mjs)
- Event ring, semihosting

### Layer 2: CircuitPython Runtime (C owns, init once)
- mp runtime (globals, builtins, qstr pool)
- GC heap (structure, not contents)
- Pystack pool
- HAL file descriptors (open fds to /hal/*)
- Display framebuffer (WasmFramebuffer singleton in linear memory)
- Supervisor terminal (terminalio Terminal object)

### Layer 3: User Session (created per RUNNING_*, destroyed in SWITCHING)
- Claimed pins
- Bus singletons (board.I2C/SPI/UART instances)
- Display root_group (user's displayio tree)
- Python heap objects (user's variables, imported module state)
- Pystack frame (running bytecode state)

## Virtual Hardware (VH)

The hardware doesn't run on its own. JS drives it:
- **preStep**: JS → MEMFS (update input pin values, time, events)
- **postStep**: MEMFS → JS (read output pin values, NeoPixel data, display)

On SWITCHING, hardware state in MEMFS must be reset. Options:
1. Zero all /hal/ files (simplest, but loses pull-up defaults etc.)
2. Restore from `initialHardwareState.json` — a snapshot of MEMFS /hal/
   state taken right after READY is entered
3. Each hardware module has a `reset()` method that restores its initial state

Option 3 is cleanest — hardware modules know their own defaults.

## Open Issues

- Display refresh after reset_displays(): does auto_refresh restart?
  Need to verify framebuffer_display_reset re-enables refresh.
- gc_collect() vs gc_sweep_all(): gc_collect marks reachable objects
  (VFS, display, runtime) and only frees user session objects. Correct.
- Cursor rendering: only during RUNNING_REPL, not RUNNING_CODE or READY.
- _loop() timing: cp_step must continue running in READY state for
  display refresh, even though no user code is executing.
