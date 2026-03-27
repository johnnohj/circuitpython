# Plan: Unified WASM-WASI Variant

## Goal

Merge the worker and reactor variants into a single standalone build that
is the board.  Drop the subordinate "worker" framing.  The resulting binary
should support both REPL and code.py execution, with the JS host acting as
a thin event forwarder — not a supervisor.

## Current state

Three variants exist:

| | standard | reactor | worker |
|---|---|---|---|
| Entry | `main.c` (`_start`) | `main_reactor.c` (reactor exports) | `main_worker.c` (`_start` + reactor exports) |
| Display | no | no | yes |
| Hardware | no | no | yes (common-hal) |
| VM yield | no | yes (`MICROPY_VM_YIELD_ENABLED`) | no |
| REPL | blocking stdin | no | event-driven |
| Code.py | via `_start` | `mp_vm_start/step` | no |
| Frozen | none | Python hw shims | Adafruit libraries |

The worker has everything except the yield mechanism.  The reactor has the
yield mechanism but nothing else.  The standard variant is the test/CLI
build and stays separate.

## What the unified variant needs

1. **VM yield** — `MICROPY_VM_YIELD_ENABLED` so code.py can run in
   cooperative bursts via `mp_vm_step()`.
2. **Display + hardware** — the worker's full common-hal, displayio,
   terminalio (with supervisor VT100 parser).
3. **Event-driven REPL** — `MICROPY_REPL_EVENT_DRIVEN` for non-blocking
   REPL input from the JS host.
4. **Dual entry** — `_start` for WASI CLI/test use, plus exported
   `init/step/push_key` functions for browser use.
5. **code.py lifecycle** — ability to switch between REPL and code.py,
   with cleanup between runs.

## Steps

### Phase 1: Enable yield in the worker build

The mechanical change is small.  The yield machinery lives in `py/vm.c`
and is gated on a single macro.

1. **Add to `variants/worker/mpconfigvariant.h`:**
   ```c
   #define MICROPY_VM_YIELD_ENABLED    (1)
   ```

2. **Add yield state save hook** (currently in `main_reactor.c`):
   ```c
   #define MICROPY_VM_YIELD_SAVE_STATE(cs) \
       mp_vm_yield_state = (void *)(cs)
   ```
   Move this to `mpconfigvariant.h` so it applies globally.

3. **Port the yield budget / yield request functions** from
   `main_reactor.c` into a shared file (e.g. `vm_yield.c`):
   - `mp_vm_should_yield()` — called by VM on each backwards jump
   - `mp_vm_set_budget(n)` — JS sets the step budget
   - `mp_vm_request_yield(reason, arg)` — common-hal requests yield
   - `mp_hal_delay_ms()` override — yields instead of blocking
   - `mp_vm_yield_state` — pointer to innermost code_state at yield

4. **Port `mp_vm_start / mp_vm_step`** from `main_reactor.c` into the
   same shared file.  These are the compile-and-step functions:
   - `mp_vm_start(path)` — compile a .py file, prepare code_state
   - `mp_vm_step()` — execute one burst, return 0/1/2
   - Export both via `__attribute__((export_name(...)))`.

5. **Build and test** — the REPL should still work.  Then test
   `mp_vm_start/step` by having JS compile and step a code.py.

### Phase 2: Clean up main_worker.c

Remove legacy two-VM artifacts and old OPFS machinery.

1. **Remove the `main()` / `_start` path** — the blocking `c_poll_loop`
   and `try_run_worker_py` functions are from the original OPFS design.
   All that remains is the reactor-style exports path (`worker_init`,
   `worker_step`, etc.).

2. **Remove OPFS constants and control file I/O** — `HW_ROOT`,
   `write_control`, `read_signal`, `ack_signal`, the signal/control-file
   layout.  These are dead code in the postMessage world.

3. **Remove `MICROPY_WORKER` flag** — grep for all `#if MICROPY_WORKER`
   and `#ifdef MICROPY_WORKER` guards.  Currently used in:
   - `wasi_mphal.c` — already removed (uses `CIRCUITPY_TERMINALIO` now)
   - `mpconfigport.h` — check what it gates
   - `Makefile` — variant identification

   Replace with feature-specific guards (`CIRCUITPY_DISPLAYIO`,
   `MICROPY_VM_YIELD_ENABLED`, etc.) where needed.

4. **Rename entry points** — `worker_init` → `cp_init`,
   `worker_step` → `cp_step`, `worker_push_key` → `cp_push_key`.
   The "worker" framing is an implementation detail of the JS host,
   not the WASM binary's identity.

5. **Consolidate init** — there are currently two init paths
   (`main()` and `worker_init()`) that duplicate GC/VFS/pystack setup.
   Merge into one `cp_init()`.

### Phase 3: Add code.py lifecycle exports

With yield enabled, the binary can run code.py cooperatively.

1. **Export `cp_run_file(path)`** — compiles and begins stepping a .py
   file.  JS calls `cp_step()` in a loop.  When step returns 0 (done)
   or 2 (exception), code.py is finished.

2. **Export `cp_run_repl()`** — switches to event-driven REPL mode.
   `cp_step()` processes one char of REPL input per call.

3. **Export `cp_reset()`** — cleanup between code.py runs and
   REPL sessions.  This is the `cleanup_after_vm` equivalent:
   - Reset pins (clear hw_state)
   - Reset display (but keep terminal alive)
   - GC collect (not full teardown — VM stays initialized)

4. **JS lifecycle** becomes:
   ```
   cp_init()
   cp_run_file("/code.py")   // or skip if no code.py
   loop: cp_step() until done
   cp_reset()
   cp_run_repl()
   loop: cp_push_key(c); cp_step()
   // Ctrl+D → cp_reset(); cp_run_file("/code.py")
   ```

### Phase 4: Variant consolidation

1. **Rename `variants/worker/`** to `variants/browser/` (or just make it
   the default variant).

2. **Remove `variants/reactor/`** — its functionality is now in the
   unified build.  `main_reactor.c` can be deleted.

3. **Remove `main_worker.c`** — replaced by a new `main_browser.c`
   (or just `main_wasm.c`) that has the consolidated init/step/lifecycle
   exports.

4. **Update Makefile** — simplify the variant-specific source blocks.
   Two variants remain: `standard` (CLI/test) and `browser` (the board).

5. **Update JS files** — `worker-offscreen.js` calls the new export
   names (`cp_init`, `cp_step`, etc.).

## Files affected

| File | Action |
|---|---|
| `variants/worker/mpconfigvariant.h` | Add `MICROPY_VM_YIELD_ENABLED`, yield save hook |
| `main_reactor.c` | Extract yield/step code, then delete |
| `main_worker.c` | Remove OPFS, consolidate init, rename exports |
| `vm_yield.c` (new) | Shared yield budget, step/start, delay-as-yield |
| `worker_terminal.c` | Remove `worker_terminal_write` (dead code after supervisor terminal switch) |
| `wasi_mphal.c` | Remove remaining `MICROPY_WORKER` references |
| `mpconfigport.h` | Remove `MICROPY_WORKER` guards |
| `Makefile` | Simplify variant blocks, add `vm_yield.c` |
| `js/worker-offscreen.js` | Update export names |
| `test_offscreen.html` | Update if export names change |

## What stays unchanged

- `variants/standard/` — CLI/test build, no display, blocking REPL
- `mpconfigboard.h` — board-level defaults (CIRCUITPY_* flags)
- `mpconfigport.h` — port-level config (VFS, paths, etc.)
- `worker_u2if.c` — hw_state diff export (still needed for board viz)
- `hw_state.c/h` — hardware state arrays
- `common-hal/` — all hardware implementations
- `js/worker-offscreen.js` — single-worker + OffscreenCanvas model
- Frozen Adafruit libraries

## Open questions

1. **Variant naming** — `browser`? `interactive`? Or just make the
   unified build the default and keep `standard` as the special case?

2. **`_start` vs pure reactor** — should the unified binary support
   both `_start` (for wasmtime/CLI testing) and reactor exports?
   Current worker already does this.  The reactor variant uses
   `-mexec-model=reactor` which prevents `_start`.  We probably want
   the command model (has `_start`) with explicit exports.

3. **cleanup_after_vm depth** — real boards do a full teardown
   (gc_deinit, pin reset, display reset, qstr reset).  For the browser,
   we probably want a lighter reset (clear pins, keep VM alive, keep
   display alive).  Need to decide what "reset" means in browser context.

4. **IDB persistence timing** — should file persistence happen on
   `cp_reset()`, or continuously?  Real boards flush filesystem on
   cleanup_after_vm.
