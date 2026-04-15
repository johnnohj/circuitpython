# Migration Plan: C Supervisor → Python main.py

## Overview

Migrate the CircuitPython WASM port's supervisor from C (`supervisor.c`, ~1000 lines) to a frozen Python `main.py` that uses asyncio as its scheduler, jsffi for JS communication, and relies on a minimal C bootstrap stub for VM initialization.

**Key architectural insight**: the existing `MICROPY_VM_HOOK_LOOP` mechanism already makes sync user code yield-cooperative. When main.py runs asyncio, and user code runs inside a task via `exec()`, backwards-branch yields bubble up through the bytecode interpreter to the asyncio event loop, which returns control to JS. No new VM changes are needed.

---

## What Stays in C (~150 lines)

```
main()              CLI: _core_init() + exec frozen __main_cli__
cp_init()           browser: _core_init() + compile/start frozen __main__
cp_step(now_ms)     browser: set frame budget + vm_yield_step (pumps main.py's asyncio)
cp_ctrl_c()         mp_sched_keyboard_interrupt()
cp_print(len)       mp_hal_stdout_tx_strn bridge
cp_input_buf_*()    shared buffer for JS→C strings
nlr_jump_fail()     fatal handler
```

`cp_init` compiles `import __main__` and starts it via `vm_yield_start`. `cp_step` calls `vm_yield_step()` once per frame — main.py's asyncio loop decides what runs. The C supervisor no longer tracks lifecycle states.

## What Moves to Python (frozen modules)

```
__main__ (main.py)           browser entry point
  ├── _supervisor             lifecycle state machine
  │   ├── asyncio             scheduler (already frozen)
  │   ├── jsffi               JS bridge (built-in C module)
  │   └── _code_runner        compile + exec user code
  │       └── traceback       error formatting
  ├── _repl                   readline, history, completion
  │   └── jsffi               editor.* calls for UI
  ├── _board_config           reads definition.json, configures pins
  │   └── json                built-in
  └── os, time, sys           built-ins

__main_cli__ (main_cli.py)   Node.js/CLI entry point
  ├── _supervisor             same lifecycle, blocking I/O
  └── sys                     stdin/stdout
```

## What Changes in JS

```
hal object (new)              stable API for Python→JS hardware calls
editor object (new)           UI integration (focus, keypresses, events)
circuitpython.mjs             simplified — fewer WASM exports to manage
readline.mjs                  gutted to thin key buffer (Python owns readline)
hardware.mjs                  adapts to hal function API (not MEMFS polling)
targets.mjs                   unchanged — reads from hal state
```

---

## JS API Surface

### `globalThis.hal` — Hardware (Python → JS)

```javascript
hal = {
    // Time (written by JS each frame)
    now_ms: number,

    // GPIO
    gpio_init(pin, direction, pull): void,
    gpio_set(pin, value): void,
    gpio_get(pin): number,
    gpio_deinit(pin): void,

    // Analog
    analog_init(pin, is_output): void,
    analog_read(pin): number,       // 0-65535
    analog_write(pin, value): void,
    analog_deinit(pin): void,

    // PWM
    pwm_init(pin, frequency, duty_cycle): void,
    pwm_set_duty(pin, duty_cycle): void,
    pwm_set_freq(pin, frequency): void,
    pwm_deinit(pin): void,

    // NeoPixel
    neopixel_init(pin, num_pixels, bpp): void,
    neopixel_write(pin, data): void,   // bytes object
    neopixel_deinit(pin): void,

    // I2C
    i2c_init(scl, sda, frequency): void,
    i2c_write(addr, data): void,
    i2c_read(addr, length): bytes,
    i2c_scan(): list,
    i2c_deinit(): void,

    // Filesystem
    read_file(path): string | null,
    write_file(path, data): void,

    // Board info (set by JS before cp_init)
    board_info: { name, pins, neopixels, displays, ... },

    // Lifecycle (Python → JS)
    set_state(state): void,       // 'booting','running','repl','done','waiting'
    print(text): void,            // displayio + serial output
}
```

### `globalThis.editor` — UI (Python ↔ JS)

```javascript
editor = {
    has_key(): boolean,
    get_key(): string,             // returns key name or char
    code_changed(): boolean,       // auto-reload notification
    write(text): void,             // serial output
}
```

---

## Phases

### Phase 1: Minimal Bootstrap — main.py Runs

**Goal**: Prove a frozen main.py can own the lifecycle while cp_step pumps it.

**Changes**:
- Create `modules/main.py` — minimal lifecycle: banner → boot.py → code.py → wait
- Modify `supervisor.c` cp_init to compile `import __main__` and start it
- Gut lifecycle state machine from supervisor.c (SUP_BOOT/CODE/REPL states)
- cp_step becomes: set frame budget → vm_yield_step → export state

**Files**:
- `modules/main.py` (new)
- `supervisor/supervisor.c` (gut lifecycle, keep bootstrap)
- `variants/*/manifest.py` (add main.py to frozen)

**Tests**: banner prints, code.py runs and finishes, Ctrl-C interrupts

**Risk**: LOW — vm_yield mechanism is proven, just starting a different file

---

### Phase 2: REPL in Python

**Goal**: Move readline/history/completion from JS+C into frozen Python.

**Changes**:
- Create `modules/_repl.py` — line editing, history, multi-line detection, tab completion
- REPL loop in main.py: `line = await readline('>>> ')` yields while waiting for keys
- Python polls `editor.has_key()` via jsffi, yields with `asyncio.sleep(0)` when empty
- Remove JS readline.mjs logic (keep only DOM keydown → editor key buffer)
- Remove C exports: cp_exec, cp_continue, cp_complete

**Files**:
- `modules/_repl.py` (new)
- `modules/main.py` (add REPL loop)
- `supervisor/supervisor.c` (remove cp_exec/cp_continue/cp_complete)
- `js/readline.mjs` (gut to thin key buffer)
- `js/circuitpython.mjs` (simplify)

**Tests**: expressions evaluate, multi-line works, tab completes, history recalls

**Risk**: MEDIUM — REPL responsiveness depends on yield timing

**Depends on**: Phase 1

---

### Phase 3: User Code Execution and Async Adaptation

**Goal**: main.py runs code.py with transparent async/sync support.

**Changes**:
- Create `modules/_code_runner.py` — compile + exec with error handling
- Sync `while True` code works via MICROPY_VM_HOOK_LOOP yield (unchanged)
- Async `asyncio.run(main())` user code nests inside main.py's asyncio loop
- Error handling: catch + format traceback, transition to "code done"
- Full lifecycle: boot.py → code.py → "Code done running." → wait → REPL → soft reboot

**Sync code flow**:
```
main.py asyncio task → exec(user_code) → while True → VM hook → YIELD_BUDGET
  → asyncio yields frame → cp_step returns → next frame resumes at branch
```

**Async code flow**:
```
main.py asyncio task → exec(user_code) → asyncio.run(user_main)
  → user's await asyncio.sleep(0.1) → YIELD_SLEEP → frame yields
```

**Files**:
- `modules/_code_runner.py` (new)
- `modules/main.py` (full lifecycle)
- `supervisor/supervisor.c` (remove _start_boot_py, _boot_to_code, etc.)

**Tests**: sync print, sync while-true, async sleep, syntax errors, runtime errors, Ctrl-C

**Risk**: MEDIUM-HIGH — nested asyncio loops are the subtle part

**Depends on**: Phase 1

---

### Phase 4: Hardware Modules in Python

**Goal**: Replace C common-hal with frozen Python calling jsffi.hal.*.

**Changes**:
- Create frozen `digitalio.py`, `analogio.py`, `pwmio.py`, `neopixel_write.py`
- Each calls jsffi.globalThis.hal methods directly
- JS `hal` object handles both state tracking AND visual updates (SVG pins)
- `_board_config.py` reads definition.json, creates `board` module with pin objects
- Remove C common-hal sources from browser variant build
- Hardware init in Python drives DOM creation in JS

**Example**:
```python
# frozen digitalio.py
import jsffi
hal = jsffi.globalThis.hal

class DigitalInOut:
    def __init__(self, pin):
        self._pin = pin.id
        hal.gpio_init(self._pin, 0, 0)  # JS: activates pin SVG

    @property
    def value(self):
        return bool(hal.gpio_get(self._pin))

    @value.setter
    def value(self, val):
        hal.gpio_set(self._pin, 1 if val else 0)  # JS: updates pin visual

    def deinit(self):
        hal.gpio_deinit(self._pin)  # JS: grays out pin SVG
```

**Files**:
- `modules/digitalio.py` (new)
- `modules/analogio.py` (new)
- `modules/pwmio.py` (new)
- `modules/neopixel_write.py` (new)
- `modules/_board_config.py` (new)
- `modules/board.py` (new, or dynamically generated)
- `js/circuitpython.mjs` (add hal object to globalThis)
- `js/hardware.mjs` (adapt to hal function calls)
- `variants/browser/mpconfigvariant.*` (disable C common-hal, add frozen modules)

**Tests**: GPIO read/write, NeoPixel show, analog read, I2C scan, hardware targets still work

**Risk**: MEDIUM — jsffi round-trip cost per operation (should match current fd_write cost)

**Depends on**: Phase 1 (can develop in parallel with 2, 3)

---

### Phase 5: Multi-Context as asyncio Tasks

**Goal**: Replace C context switching (pystack swap) with asyncio tasks.

**Changes**:
- "Contexts" become asyncio tasks: `asyncio.create_task(_exec_code(code))`
- Remove `context.c`, `context.h`, and C scheduler
- JS `runCode()` / `killContext()` become jsffi calls that Python handles
- Cancellation: `task.cancel()` raises CancelledError in the running code

**Files**:
- `supervisor/context.c` (remove)
- `supervisor/context.h` (remove)
- `modules/main.py` (add task management)
- `js/circuitpython.mjs` (remove context meta reading, scheduler pick)

**Tests**: background tasks run, listContexts shows them, killContext cancels them

**Risk**: HIGH — asyncio tasks share pystack (no isolation). Acceptable for browser sim.

**Depends on**: Phases 1, 3

---

### Phase 6: Cleanup and CLI Variant

**Goal**: Final cleanup, `main_cli.py`, dead code removal.

**Changes**:
- Create `modules/main_cli.py` — same lifecycle with blocking I/O (no asyncio, no jsffi)
- `supervisor.c` main() starts `__main_cli__`
- Remove dead code: vm_yield stepping state, compile.c, most of readline.mjs
- Reduce WASM exports to minimum
- Final regression testing

**Files**: everything touched above, final cleanup pass

**Risk**: LOW (cleanup)

**Depends on**: All previous phases

---

## Key Design Answers

### How does asyncio integrate with requestAnimationFrame?

JS calls `cp_step()` each frame. `cp_step()` calls `vm_yield_step()` which resumes main.py. Main.py's asyncio loop runs one iteration, processes ready tasks, then yields:
- If sleeping: `time.sleep(dt)` triggers YIELD_SLEEP → vm_yield_step returns
- If idle: `await asyncio.sleep(0)` at end of event loop → YIELD_BUDGET → returns
Either way, control returns to JS within the frame budget (~13ms).

### How does `while True` without await work?

Exactly as today. MICROPY_VM_HOOK_LOOP fires at backwards branches, checks wall-clock budget, triggers YIELD_BUDGET. The VM returns MP_VM_RETURN_YIELD, which unwinds through `exec()` in _code_runner.py, through main.py's asyncio task, back to the event loop, which yields. Next cp_step resumes at the branch.

### What's the failsafe if main.py crashes?

`vm_yield_step()` returns 2 (exception). The C stub prints the traceback and re-compiles `import __main__`. A counter limits restarts to 3 per second to prevent crash loops.

### How does Ctrl-D (soft reboot) work?

Ctrl-D on empty REPL line: `_repl.py` raises SystemExit. Main.py's outer `while True` lifecycle loop catches it, prints "soft reboot", and restarts the iteration (re-runs boot.py → code.py → ...).

For hard reset (clear GC heap): C `cp_ctrl_d()` calls `gc_init()` + `mp_init()` + re-starts main.py.

### Thread/mutex?

Single-threaded, no real mutexes needed. `mp_thread_begin_atomic_section()` prevents VM hook yields during critical sections. Stays in C, unaffected by migration.

### Code analysis — JS or Python?

Python handles all analysis. `compile()` detects syntax errors. To detect async main, inspect `co_flags`. No JS pre-parsing needed. JS provides `editor.code_changed()` for auto-reload.

---

## Migration Order Summary

```
Phase 1 (bootstrap)     → main.py runs, lifecycle in Python
Phase 2 (REPL)          → readline/history/completion in Python    [depends: 1]
Phase 3 (code runner)   → async/sync user code adaptation          [depends: 1]
Phase 4 (hardware)      → Python modules via jsffi.hal             [depends: 1, parallel w/ 2,3]
Phase 5 (contexts)      → asyncio tasks replace C contexts         [depends: 1, 3]
Phase 6 (cleanup)       → main_cli.py, dead code removal           [depends: all]
```

Phases 2, 3, and 4 can be developed in parallel after Phase 1.
Each phase is independently testable and shippable.
