# Port Consolidation: Criteria and Specifications

**Status**: Design (2026-04-26)
**Depends on**: abort-resume (proven), chassis PoC (proven), board-as-ui (design)

## Problem

The `ports/wasm/` directory has two generations of code:

- **supervisor/** — pre-chassis code, written before MEMFS-in-linear-memory
  and abort-resume were proven.  Contains production-essential features
  (context scheduling, compilation, display, tick system) but also
  superseded mechanisms (SUSPEND sentinel, FD-based HAL, vm_yield).

- **chassis/** — newer, stack-safe patterns proven via standalone tests.
  Contains the right memory model and halt/resume protocol but lacks the
  full supervisor lifecycle, display integration, and context scheduling.

Additionally, root-level files (main.c, wasi_mphal.c, proxy_c.c, etc.)
have unclear ownership boundaries, and the JS layer has accumulated
overlapping responsibilities (semihosting.js vs chassis-api.mjs, etc.).

The goal is to consolidate into a single coherent port with:
- Chassis-proven memory model and abort-resume protocol
- Supervisor-proven lifecycle, context scheduling, and display
- Clean layer boundaries: port | JS runtime | VM
- Build variants via boards, not ad-hoc Makefile conditionals

## The cardinal rule

**No state on the C stack that must survive a frame boundary.**

Every function in the port must be evaluated against this criterion.
If a function allocates state in C local variables that the port needs
after the function returns (or after an nlr_jump_abort), that state must
move to port_mem or the GC heap.

This is the single most important specification.  Every other criterion
follows from it.

### How to test

For any function `f()` in the port:

1. Can `nlr_jump_abort()` fire while `f()` is on the call stack?
2. If yes: does `f()` hold state in local variables that the port needs
   after the abort lands?
3. If yes: that state must move to port_mem (for port state) or the GC
   heap (for VM state).

Functions that only run outside the VM (during event drain, HAL step,
or state export phases of the frame) are not subject to abort and can
use locals freely.

### Vocabulary

- **frame-transient**: state that exists only during one `chassis_frame()`
  call.  C locals are fine.  Example: loop counters in `port_drain_events()`.

- **frame-durable**: state that must survive across frame boundaries.
  Must live in port_mem.  Example: VM code_state, event ring head/tail,
  pin claim bitmask.

- **VM-durable**: state that must survive across VM restarts (soft reboot).
  Must live in port_mem outside the VM region.  Example: pin categories,
  board identity, serial ring buffers.

## Layer model

```
┌─────────────────────────────────────────────────────────┐
│  Layer C: CircuitPython supervisor + VM                 │
│                                                         │
│  Adapted upstream code.  Follows CP conventions.        │
│  Files: supervisor/, common-hal/, shared-bindings/      │
│  Owns: VM lifecycle, context scheduling, compilation,   │
│        display, REPL, boot.py/code.py sequencing        │
│                                                         │
│  May call Layer B.  Never calls Layer A directly.       │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│  Layer B: Port (coordination/translation)               │
│                                                         │
│  Our code.  The "firmware."                             │
│  Files: port core (main, port_memory, budget, serial,   │
│         hal, port_step, vm_abort, gccollect)            │
│  Owns: frame loop, memory layout, abort-resume,         │
│        budget enforcement, event ring, FFI imports,     │
│        serial ring buffers, HAL claim/release           │
│                                                         │
│  Provides services to Layer C.  Uses Layer A for I/O.   │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│  Layer A: WASI runtime + JS                             │
│                                                         │
│  JS code.  The "host."                                  │
│  Files: js/, test HTML, board definitions               │
│  Owns: DOM, rendering, user input, WASI filesystem,     │
│        IndexedDB persistence, rAF scheduling,           │
│        board SVG, component simulation                  │
│                                                         │
│  Calls Layer B via WASM exports.  Layer B calls         │
│  Layer A via WASM imports.                              │
└─────────────────────────────────────────────────────────┘
```

### Layer boundary rules

1. **Layer C never calls JS.**  It calls Layer B port functions, which
   may internally use WASM imports.  Layer C code should compile without
   any `import_module` attributes.

2. **Layer B owns all WASM imports and exports.**  Every function with
   `__attribute__((import_module(...)))` or `__attribute__((export_name(...)))`
   lives in Layer B.

3. **Layer A never writes to port_mem directly.**  It uses WASM exports
   (Layer B functions) or writes to MEMFS regions that Layer B registered.
   The event ring is the exception: JS writes events, C reads them —
   but the format is defined by Layer B.

4. **Common-hal is Layer C code that calls Layer B HAL functions.**
   `common_hal_digitalio_digitalinout_set_value()` calls `hal_write_pin()`,
   not a WASM import.  The HAL is the abstraction boundary.

## File consolidation plan

### Current → Proposed directory structure

```
ports/wasm/
├── boards/
│   ├── browser/                    # was: variants/browser/ + boards/wasm_browser/
│   │   ├── definition.json
│   │   ├── board.svg
│   │   ├── mpconfigboard.h        # CIRCUITPY_* flags for browser
│   │   ├── mpconfigboard.mk
│   │   └── pins.c                 # generated or hand-maintained
│   ├── worker/                     # future: Web Worker context
│   │   └── ...
│   └── standard/                   # was: variants/standard/
│       ├── mpconfigboard.h
│       ├── mpconfigboard.mk
│       └── pins.c
│
├── common-hal/                     # Layer C: unchanged structure
│   ├── digitalio/
│   ├── analogio/
│   ├── busio/
│   ├── displayio/
│   ├── microcontroller/
│   ├── neopixel_write/
│   ├── pwmio/
│   └── ...
│
├── supervisor/                     # Layer C: CP supervisor adaptation
│   ├── port.c                      # port_init, port_deinit, reset_port
│   ├── serial.c                    # display/console routing (uses Layer B serial ring)
│   ├── context.c/h                 # multi-context scheduling
│   ├── compile.c/h                 # cp_compile_str/file
│   ├── tick.c                      # lightweight/heavyweight tick
│   ├── background_callback.c       # callback queue
│   ├── display.c                   # displayio backend
│   ├── status_bar.c                # status bar rendering
│   └── stubs.c                     # weak overrides
│
├── port/                           # Layer B: port core (was: chassis/ + root files)
│   ├── main.c                      # chassis_init + chassis_frame entry points
│   ├── port_memory.c/h             # MEMFS-native memory layout (from chassis)
│   ├── budget.c/h                  # frame budget tracking (from chassis)
│   ├── serial.c/h                  # ring buffers in port_mem (from chassis)
│   ├── hal.c/h                     # direct-memory HAL (from chassis, + categories from supervisor)
│   ├── vm_abort.c                  # abort-resume protocol (from chassis)
│   ├── port_step.c/h               # frame phase state machine (from chassis)
│   ├── event_ring.c/h              # event drain (from supervisor/semihosting.c)
│   ├── ffi_imports.h               # WASM imports (port→JS)
│   ├── ffi_exports.c               # WASM exports (JS→port)
│   ├── constants.h                 # shared constants (RC_*, event types, HAL offsets)
│   └── macros.h                    # inline helpers, PLACE_IN_DTCM
│
├── ffi/                            # Layer B: JS↔Python proxy (unchanged)
│   ├── proxy_c.c/h                 # PVN marshaling, reference tables
│   ├── objjsproxy.c                # JsProxy type
│   ├── modjsffi.c                  # jsffi module
│   └── jsffi_imports.h             # high-level WASM imports (proxy ops)
│
├── modules/                        # frozen Python modules
│   └── asyncio/
│
├── js/                             # Layer A: JS runtime
│   ├── wasi.js                     # was: wasi-memfs.js (unified, with aliased file support)
│   ├── ffi.js                      # was: jsffi.js (JS-side proxy)
│   ├── board.mjs                   # was: circuitpython.mjs (board runtime, frame loop)
│   ├── display.mjs                 # framebuffer rendering
│   ├── hardware.mjs                # MEMFS hardware state reads
│   ├── serial.mjs                  # was: readline.mjs + shell.mjs (serial terminal)
│   ├── env.mjs                     # was: env.js (runtime detection)
│   └── targets.mjs                 # external hardware (WebUSB, WebSerial)
│
├── tests/                          # all tests in one place
│   ├── test-chassis.mjs
│   ├── test-ffi.mjs
│   ├── test-vm-ready.mjs
│   ├── test-abort-resume.mjs
│   └── test-browser.html
│
├── design/                         # design documents (unchanged)
│
├── main.c                          # top-level: CLI entry (standard variant only)
├── gccollect.c                     # GC root scanning
├── wasi_mphal.c                    # WASI platform HAL
├── mpthreadport.c/h                # single-threaded atomics
├── mphalport.h                     # HAL config macros
├── mpconfigport.h                  # port-level feature flags
├── modmachine.c                    # machine module
├── modos.c                         # os module
├── modringio.c                     # RingIO stream
├── board_display.c/h               # framebuffer display init
├── wasm_framebuffer.c/h            # RGB565 framebuffer protocol
├── qstrdefsport.h
└── Makefile
```

### Key changes

1. **chassis/ → port/**: The chassis IS the port core.  Rename to reflect
   that it's not a prototype — it's the production code.

2. **variants/ → boards/**: Adopt CP board convention.  Each "board" is a
   build target.  `browser` replaces `variants/browser/` + `boards/wasm_browser/`.

3. **ffi/**: Group proxy-related files together.  They're currently scattered
   across root (proxy_c.c, objjsproxy.c, modjsffi.c, jsffi_imports.h).

4. **JS naming**: Standardize on `.mjs` for all ES modules.  Rename for
   clarity (circuitpython.mjs → board.mjs, readline.mjs + shell.mjs → serial.mjs).

5. **tests/**: Consolidate all test files into one directory.

## Function audit criteria

For every function in the port, apply these tests:

### Test 1: Stack safety

```
Q: Does this function create C local state that must survive a frame boundary?
A: If yes → FAIL.  Move state to port_mem or GC heap.
```

**Examples of violations:**
```c
// BAD: local buffer holds partial parse state across yields
void supervisor_process_serial(void) {
    char buf[256];      // ← frame-transient, but what if we abort mid-parse?
    int len = read(rx_fd, buf, sizeof(buf));
    parse_packet(buf, len);  // ← if this calls VM code that aborts...
}

// GOOD: state lives in port_mem
void port_process_serial(void) {
    // Ring buffer head/tail are in port_mem — survive across frames
    int avail = serial_rx_available();
    if (avail > 0) {
        // Read into port_mem scratch area, not C local
        serial_rx_read(port_mem.serial_scratch, avail);
        port_mem.serial_pending = avail;
    }
}
```

### Test 2: Layer compliance

```
Q: Does this function call across more than one layer boundary?
A: If yes → FAIL.  Insert a Layer B intermediary.
```

**Examples of violations:**
```c
// BAD: Layer C (common-hal) calls Layer A (WASM import) directly
void common_hal_digitalio_digitalinout_construct(...) {
    jsffi_register_pin_listener(pin->number);  // ← skips Layer B
}

// GOOD: Layer C calls Layer B, which calls Layer A
void common_hal_digitalio_digitalinout_construct(...) {
    hal_claim_pin(pin->number, HAL_ROLE_DIGITAL_IN);  // Layer B
    // hal_claim_pin internally calls ffi_notify() (Layer A)
}
```

### Test 3: Object lifetime

```
Q: Does this function create objects that outlive the current frame?
A: If yes → where do they live?
   - port_mem: correct for port state
   - GC heap: correct for VM state (survives frames, collected on soft reboot)
   - C stack: FAIL
   - malloc: FAIL (no free point guaranteed — abort can fire anytime)
```

**Port-level GC consideration:**  If the port needs dynamically-sized
allocations that outlive a frame but aren't VM objects (e.g., event
queues, packet buffers), we have two options:

A. **Fixed regions in port_mem**: pre-allocate maximum-size buffers.
   Simple, wastes memory, but predictable.  Preferred for known-size data.

B. **Port-local arena allocator**: a simple bump allocator in a port_mem
   region, reset at known points (frame start, soft reboot).  No individual
   free — just reset the bump pointer.  Appropriate for per-frame scratch.

We do NOT need a full GC for the port layer.  The VM GC handles Python
objects.  The port needs only fixed buffers and occasional scratch space.

### Test 4: Naming convention

```
C functions:
  Layer B port:     port_*  or  hal_*  or  serial_*  or  budget_*
  Layer C supervisor: supervisor_*  or  cp_*  or  common_hal_*
  WASM imports:     ffi_*  (low-level)  or  jsffi_*  (proxy)
  WASM exports:     __attribute__((export_name("wasm_*")))

C types:
  port_memory_t, port_state_t, port_event_t, hal_pin_slot_t

JS functions:
  camelCase throughout
  Module filenames: kebab-case.mjs (e.g., wasi-runtime.mjs)

Constants:
  C:  UPPERCASE with category prefix (HAL_ROLE_*, RC_*, SH_EVT_*)
  JS: UPPERCASE mirroring C (same names)
```

### Test 5: Build variant isolation

```
Q: Does this file compile for all variants, or only some?
A: If variant-specific → it belongs in boards/{variant}/ or is
   guarded by a CIRCUITPY_* flag defined in mpconfigboard.h.
```

The Makefile should never contain variant-specific source file lists.
Instead, boards/{variant}/mpconfigboard.mk sets flags, and source files
use `#if CIRCUITPY_*` guards.

## Consolidation procedures

### Procedure A: Back-porting a chassis pattern

When moving a chassis function into the production port:

1. **Read the chassis version.**  Note its type signatures, what it reads
   from port_mem, what it writes, and what WASM imports it uses.

2. **Read the supervisor version** (if one exists).  Note what additional
   functionality it provides that the chassis version lacks.

3. **Write the consolidated version** in port/ (Layer B) or supervisor/
   (Layer C) depending on which layer owns the concern.

4. **Apply Test 1** (stack safety): verify no locals survive frames.

5. **Apply Test 2** (layer compliance): verify it doesn't skip layers.

6. **Apply Test 3** (object lifetime): verify all durable state is in
   port_mem or GC heap.

7. **Write a test** that exercises the function across an abort boundary:
   call the function, trigger nlr_jump_abort mid-execution, verify state
   is consistent after resume.

### Procedure B: Evaluating a supervisor function for port fitness

When deciding whether to keep, modify, or replace a supervisor function:

1. **Does it create C stack state?**  Check for local arrays, buffers,
   structs that hold state across a yield/abort point.

2. **Does it call the VM?**  If yes, an abort can fire at any point during
   that call.  All state that must survive the abort must be in port_mem
   before the VM call.

3. **Does it use FD-based I/O for hot-path state?**  If yes, consider
   replacing with direct port_mem access (chassis pattern).  FD calls
   are appropriate for cold paths (init, config) but too expensive for
   per-frame state reads.

4. **Does it implement functionality we need?**  Some supervisor functions
   are essential (context scheduling, compilation) even if their
   implementation needs rework.  Preserve the intent; rewrite the
   implementation.

5. **Does it match CircuitPython upstream patterns?**  If a function
   mirrors `supervisor/shared/*.c` from upstream CP, preserve the API
   even if the implementation differs.  This eases future convergence.

### Procedure C: Adopting patterns from other ports

When examining another port's implementation (unix, raspberrypi, etc.):

1. **Read the function signature and documentation.**  This is the
   contract we must honor.

2. **Ignore the implementation.**  Other ports write to hardware registers,
   use DMA, manipulate interrupt controllers.  None of that applies except by analogy.

3. **Map the intent to our layers:**
   - Hardware register write → MEMFS slot write (port_mem)
   - Interrupt enable/disable → event ring subscribe/unsubscribe
   - DMA transfer → JS-side async operation + event notification
   - Sleep/WFI → budget yield (abort back to chassis)
   - Critical section → mpthreadport atomic counter

4. **Check that the mapping is stack-safe.**  The most common pitfall:
   other ports use blocking waits (`while (!ready) { ... }`) that
   assume the C stack persists.  We cannot block.  Every wait must
   become an abort-and-resume at the next frame.

## Specific file decisions

### Files to adopt from chassis (chassis → port/)

| Chassis file | Destination | Notes |
|---|---|---|
| port_memory.c/h | port/port_memory.c/h | Add supervisor state fields from supervisor/port_memory.h |
| budget.c/h | port/budget.c/h | As-is |
| serial.c/h | port/serial.c/h | As-is (ring buffers) |
| hal.c/h | port/hal.c/h | Add pin categories from supervisor/hal.c |
| vm_abort.c | port/vm_abort.c | As-is |
| port_step.c/h | port/port_step.c/h | Extend with VM phases |
| main.c | port/main.c | Extend with CP supervisor lifecycle |
| chassis_constants.h | port/constants.h | Rename |
| ffi_imports.h | port/ffi_imports.h | As-is |
| memfs_imports.h | port/memfs_imports.h | As-is |
| port_macros.h | port/macros.h | Rename |

### Files to preserve from supervisor (supervisor/ stays)

| File | Reason |
|---|---|
| context.c/h | Multi-context scheduling — essential, no chassis equivalent |
| compile.c/h | Unified compilation — essential, no chassis equivalent |
| tick.c | Two-level tick — essential, rework to use port/budget |
| background_callback.c | Callback queue — essential |
| display.c | Displayio backend — essential |
| port.c | Port init/deinit — rework to use port/port_memory |
| serial.c | Display/console routing — rework to use port/serial ring buffers |
| status_bar.c | Status bar — keep |
| stubs.c | Weak overrides — keep |

### Files to remove or merge

| File | Action | Reason |
|---|---|---|
| supervisor/port_memory.c/h | Remove | Replaced by port/port_memory.c/h |
| supervisor/hal.c/h | Remove | Replaced by port/hal.c/h |
| supervisor/semihosting.c/h | Merge into port/event_ring.c | Event ring draining moves to Layer B |
| supervisor/vm_yield.c | Remove | Superseded by port/vm_abort.c |
| supervisor/supervisor_internal.h | Remove | Internals absorbed by port/main.c |
| supervisor/port_heap.h | Remove | Absorbed by port/port_memory.h |
| supervisor/linker.h | Remove | No linker script on WASM |
| supervisor/micropython.c | Merge into stubs.c | Weak stubs |
| supervisor/port_imports.h | Remove | Replaced by port/ffi_imports.h |

### JS files to rename/consolidate

| Current | Proposed | Reason |
|---|---|---|
| js/circuitpython.mjs | js/board.mjs | Clearer: this is the board runtime |
| js/semihosting.js | (absorbed into js/board.mjs) | Semihosting concept is gone; event ring + state export live in board runtime |
| js/readline.mjs + js/shell.mjs | js/serial.mjs | One serial terminal module |
| js/env.js | js/env.mjs | Consistent extension |
| js/fwip.js | js/fwip.mjs | Consistent extension |
| js/board-adapter.mjs | js/board-adapter.mjs | Keep (pin name mapping) |
| chassis/wasi-chassis.js | (absorbed into js/wasi.js) | Unified WASI with aliased file support |
| chassis/chassis-api.mjs | (absorbed into js/board.mjs) | No separate chassis API |
| chassis/chassis-constants.mjs | js/constants.mjs | Mirror of port/constants.h |

## Build system: boards convention

### Current (variants)

```makefile
# Makefile
VARIANT ?= standard
include variants/$(VARIANT)/mpconfigvariant.mk
BUILD = build-$(VARIANT)
```

### Proposed (boards)

```makefile
# Makefile
BOARD ?= standard
BOARD_DIR = boards/$(BOARD)
include $(BOARD_DIR)/mpconfigboard.mk
BUILD = build-$(BOARD)
```

Each board directory contains:
- `mpconfigboard.h` — CIRCUITPY_* flags (was mpconfigvariant.h + mpconfigboard.h)
- `mpconfigboard.mk` — build flags (was mpconfigvariant.mk)
- `pins.c` — board pin table (was common-hal/board/board_pins.c)
- `definition.json` — visual/electrical definition (browser boards)
- `board.svg` — board artwork (browser boards)

The standard board has no definition.json or board.svg (it's a CLI REPL,
no visual representation).

## Convergence with CircuitPython main.c

Upstream `main.c` has this lifecycle:

```
hard_reset
 └→ safe_mode_check
     └→ filesystem_init (mount CIRCUITPY)
         └→ start_mp (stack, pystack, GC, mp_init)
             └→ boot.py
                 └→ cleanup_after_vm
                     └→ loop:
                         ├→ start_mp
                         ├→ code.py (or code.txt, main.py, etc.)
                         ├→ cleanup_after_vm
                         ├→ REPL (if not auto-reload)
                         ├→ cleanup_after_vm
                         └→ wait for reload trigger
```

Our `port/main.c` should follow the same structure, but each blocking
call becomes an abort-resume point:

```
chassis_init:
  port_memory_init
  hal_init (populate template slots from definition.json)
  budget_init
  serial_init
  mp_thread_init  ← before mp_init (unix port pattern)
  start_mp (stack, pystack, GC, mp_init)
  mount CIRCUITPY (WASI VFS)
  port_state.phase = PHASE_BOOT

chassis_frame(now_ms, budget_ms):
  budget_begin(now_ms, budget_ms)
  port_drain_events()      ← process JS input
  serial_drain_rx()        ← feed serial ring to VM input

  switch (port_state.phase):
    PHASE_BOOT:
      run boot.py via abort-resume
      if done → cleanup_after_vm, phase = PHASE_CODE

    PHASE_CODE:
      run code.py via abort-resume
      if done → cleanup_after_vm, phase = PHASE_REPL_OR_WAIT

    PHASE_REPL_OR_WAIT:
      if auto_reload_pending → phase = PHASE_RELOAD
      if serial_input → phase = PHASE_REPL

    PHASE_REPL:
      run REPL line via abort-resume
      if done → check result, stay in PHASE_REPL or PHASE_CODE

    PHASE_RELOAD:
      cleanup_after_vm
      start_mp (re-init)
      phase = PHASE_CODE

  hal_step()               ← process dirty flags, wake dependents
  serial_drain_tx()        ← flush output ring
  budget_end()
  return status + wakeup_ms
```

The key insight: `run_code_py()` in upstream is a blocking function with
an inner loop.  Ours is a state machine that advances one step per frame.
The abort-resume mechanism makes each "step" safe — the VM runs until
budget expires, aborts back to chassis_frame, and resumes next frame.

## Verification checklist

Before merging any consolidated code:

- [ ] `nlr_jump_abort()` from any point in the VM lands safely at the
      chassis boundary and state is resumable
- [ ] No function in port/ uses malloc (only port_mem and GC heap)
- [ ] No function in supervisor/ calls a WASM import directly
- [ ] All WASM imports are in port/ffi_imports.h or ffi/jsffi_imports.h
- [ ] All WASM exports are in port/ffi_exports.c
- [ ] `rm -rf build-*` + full rebuild succeeds for all boards
- [ ] test-chassis.mjs passes (MEMFS aliasing, frame loop, GPIO)
- [ ] test-vm-ready.mjs passes (abort-resume, expression eval)
- [ ] test-abort-resume.mjs passes (31-frame tight loop)
- [ ] REPL works in browser (type expression, see result)
- [ ] LED toggle from Python updates SVG in browser

## On stubs, threads, and dismissed functionality

A recurring mistake in this port's history has been to dismiss upstream
functionality too quickly on the grounds that "we don't have real X."
We don't have real threads, so threading is no-op.  We don't have real
interrupts, so ISR machinery is stubbed out.  We don't have real
hardware, so certain HAL paths are shortcut.

This is wrong for three reasons:

### 1. Stubs are signals, not dead ends

A stub exists because something upstream depends on it.  When we write
`void port_enable_tick(void) {}` or `mp_uint_t mp_thread_get_id(void)
{ return 1; }`, we are not implementing nothing — we are making a claim
that the function's *effect* is unnecessary on this port.  That claim
must be re-evaluated whenever the port's capabilities change.

Example: `mpthreadport.c` was originally written as a no-op because
"WASM is single-threaded."  But CircuitPython uses thread functions to
support asyncio, and the port itself is effectively a thread running
inside a JS event loop.  The thread init/deinit lifecycle
(`mp_thread_init` is called BEFORE `mp_init` on unix) reveals
assumptions about execution context setup that we ignored.  Our
multi-context scheduler is a threading model — it just doesn't use
OS threads.

**Rule**: Every stub must have a comment explaining WHY the effect is
unnecessary, not just that it is.  During consolidation, revisit each
stub and ask: "Given abort-resume, MEMFS, the event ring, and the
frame loop — is this still unnecessary, or does our port now provide
the substrate to implement it properly?"

### 2. Simulated is not absent

We run simulated hardware.  A simulated ISR (the abort mechanism
triggered by `mp_sched_vm_abort`) is not a fake ISR — it is our port's
real interrupt mechanism, as real to Python code as a GPIO interrupt on
a Cortex-M.  The DOM IS the interrupt controller.  A button click IS
a hardware interrupt.  The event ring IS the interrupt vector table.

This means we should implement interrupt-adjacent functionality that
other ports implement, not dismiss it:

- **`port_enable_tick` / `port_disable_tick`**: On real hardware, these
  enable/disable a periodic timer interrupt.  On our port, they
  control whether the frame loop runs background tasks.  Not a no-op.

- **`mp_sched_schedule`**: Schedules a Python callback from "interrupt
  context."  On our port, "interrupt context" is the event drain phase
  of the frame loop — outside the VM's NLR boundary.  We can schedule
  callbacks from event handlers.  Not a no-op.

- **`common_hal_mcu_disable_interrupts` / `enable_interrupts`**: On
  real hardware, masks the NVIC.  On our port, this could set a flag
  that suppresses event ring draining during a critical section.
  Whether we need this depends on whether common-hal code relies on
  interrupt masking for atomicity.

- **Watchdog timer**: On real hardware, resets the MCU if the main loop
  stalls.  On our port, a watchdog could detect budget overruns or
  infinite loops that the abort mechanism fails to catch (e.g., tight
  loops in C code, not bytecode).

### 3. Dismissal compounds

Each individually-dismissed feature creates a gap.  Multiple gaps
compound into a port that behaves subtly differently from real hardware.
A user's code works in the browser but fails on a Feather, or vice
versa, because our port silently no-ops something that real hardware
does.

The aspiration is that code written for a browser board runs unchanged
on a real board (within hardware limits).  This means:

- If real boards have interrupt-driven pin change detection, we need
  interrupt-driven pin change detection (event ring + abort).
- If real boards have a background task system that runs between VM
  ticks, we need the same (the tick system, not a no-op).
- If real boards support `countio`, `rotaryio`, `pulseio` via
  hardware timers and interrupts, we need simulation equivalents
  (JS-side counters, event-driven edges).

### Consolidation procedure for stubs

For each stub function during consolidation:

1. **Find the caller.**  Who calls this function?  What does the caller
   expect to happen?

2. **Find a real implementation.**  Read the raspberrypi or atmel-samd
   port's version.  What effect does it have?

3. **Map the effect to our substrate:**
   - Hardware timer → JS setTimeout / rAF timing
   - Interrupt mask → event ring suppression flag
   - DMA channel → JS async operation
   - GPIO register → MEMFS slot
   - Sleep/WFI → abort back to chassis (cooperative yield)
   - Thread context switch → context scheduler pick

4. **If the mapping exists, implement it.**  Even if the implementation
   is simple, it should be correct.  A one-line function that sets a
   flag in port_mem is better than an empty function that silently
   drops the request.

5. **If the mapping genuinely doesn't exist** (e.g., we have no USB
   hardware, so USB stubs remain empty), document why in the stub with
   a comment referencing the specific hardware dependency.

## Open questions

1. **Port-level allocator**: Do we need one?  The chassis PoC uses only
   fixed regions.  If we discover a need for variable-size port state
   (e.g., dynamic component lists), a bump allocator in port_mem with
   per-frame reset is the simplest option.

2. **supervisor/ naming**: Should we keep the `supervisor/` directory
   name for Layer C code, or rename it to match upstream more closely?
   Keeping `supervisor/` maintains familiarity with CircuitPython
   conventions.

3. **Incremental migration**: Should we migrate file-by-file (risky:
   half-old half-new for a while) or do a single large reorganization
   commit (risky: hard to review)?  Recommendation: migrate in phases,
   one layer at a time (port/ first, then supervisor/ rework, then JS).

4. **Common-hal regression**: Some common-hal files use `hal_gpio_fd()`
   and `lseek/read/write`.  These must be updated to use the new
   `hal_write_pin/hal_read_pin` direct-memory API.  This is mechanical
   but touches many files.

5. **Two main.c files**: Root `main.c` is the CLI entry point (standard
   board).  `port/main.c` is the frame loop entry point (browser board).
   Should we unify them with a `#if` guard, or keep them separate?
   Recommendation: keep separate — they serve fundamentally different
   execution models (blocking CLI vs. frame-driven browser).
