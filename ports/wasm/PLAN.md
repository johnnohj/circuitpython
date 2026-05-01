# Migration Plan: ports/wasm → ports/wasm-tmp → ports/wasm

**Created**: 2026-04-27
**Source**: `ports/wasm/design/port-consolidation.md` + related design docs
**Method**: Incremental migration with review at each chunk

## Principles

1. **Review-first**: Each chunk lands in `wasm-tmp/`, user reviews with
   inline comments before the next chunk proceeds.
2. **No dead code**: Every file brought over must compile (or be clearly
   marked as needing a dependency from a later phase).
3. **Stubs are intentional**: Per the "stubs and dismissed functionality"
   section of port-consolidation.md, every stub must document WHY the
   effect is unnecessary, not just that it is. Stubs are re-evaluated
   against the current substrate (abort-resume, MEMFS, event ring, frame
   loop) before being carried forward.
4. **Documentation tracks reality**: Design docs are updated as files
   migrate — not after.
5. **Double-check on entry and exit**: Files are audited when brought
   into wasm-tmp (does it meet consolidation criteria?) and again before
   the final wasm-tmp → wasm swap (does it still match the design docs?).

## Target directory structure

```
ports/wasm-tmp/
├── port/                    # "wasm layer" — the simulation contract
│   │                        # See design/wasm-layer.md
│   ├── main.c               # chassis_init + chassis_frame
│   ├── port_memory.c/h      # MEMFS-native memory layout
│   ├── budget.c/h           # frame budget tracking
│   ├── serial.c/h           # ring buffers in port_mem
│   ├── hal.c/h              # peripheral simulation (common-hal contract)
│   ├── wasi_mphal.c         # platform services (mp_hal_* contract)
│   ├── vm_abort.c           # abort-resume protocol
│   ├── port_step.c/h        # frame phase state machine
│   ├── event_ring.c/h       # event drain
│   ├── ffi_imports.h        # WASM imports (port→JS)
│   ├── ffi_exports.c        # WASM exports (JS→port)
│   ├── constants.h          # shared constants
│   └── macros.h             # inline helpers
│
├── ffi/                     # Layer B: JS↔Python proxy
│   ├── proxy_c.c/h          # PVN marshaling
│   ├── objjsproxy.c         # JsProxy type
│   ├── modjsffi.c           # jsffi module
│   └── jsffi_imports.h      # high-level WASM imports
│
├── supervisor/              # Layer C: CP supervisor adaptation
│   ├── port.c               # port_init, port_deinit, reset_port
│   ├── serial.c             # display/console routing
│   ├── context.c/h          # multi-context scheduling
│   ├── compile.c/h          # cp_compile_str/file
│   ├── tick.c               # lightweight/heavyweight tick
│   ├── background_callback.c
│   ├── display.c            # displayio backend
│   ├── status_bar.c
│   └── stubs.c              # weak overrides (each one documented)
│
├── common-hal/              # Layer C: hardware abstraction
│   └── (subdirs as current)
│
├── boards/                  # build targets (was variants/)
│   ├── standard/
│   │   ├── mpconfigboard.h
│   │   ├── mpconfigboard.mk
│   │   └── pins.c
│   └── browser/
│       ├── mpconfigboard.h
│       ├── mpconfigboard.mk
│       ├── pins.c
│       ├── definition.json
│       └── board.svg
│
├── js/                      # Layer A: JS runtime
│   ├── wasi.js              # was wasi-memfs.js
│   ├── ffi.js               # was jsffi.js
│   ├── board.mjs            # was circuitpython.mjs
│   ├── display.mjs
│   ├── hardware.mjs
│   ├── serial.mjs           # was readline.mjs + shell.mjs
│   ├── env.mjs              # was env.js
│   └── targets.mjs
│
├── tests/                   # all tests consolidated
│
├── modules/                 # frozen Python
│
├── design/                  # design docs (carried over, updated)
│
├── (root port files)        # main.c (CLI), gccollect.c, mphalport.h, etc.
├── Makefile
└── PLAN.md                  # this file
```

## Phases

Each phase is a reviewable chunk. Within each phase, files are delivered
in small batches (1-4 files) to keep reviews manageable.

---

### Phase 0: Foundation — build system + configuration

**Goal**: Establish the skeleton so subsequent files have a home and we
can verify compilation incrementally.

**Chunks**:

0.0 **design/behavior/ — Expected port/board behavior guide**
    - Split into separate files for reviewability:
      - `README.md` — index + upstream lifecycle reference
      - `01-hardware-init.md` — port_init, pin reset, heap init
      - `02-serial-and-stack.md` — serial, safe mode, stack
      - `03-filesystem.md` — filesystem, reset port/board, board init
      - `04-script-execution.md` — auto-reload, safemode/boot/code.py, REPL
      - `05-vm-lifecycle.md` — start_mp, cleanup_after_vm
      - `06-runtime-environments.md` — Node CLI vs browser vs Web Worker
      - `07-deviations.md` — structural, environmental, intentional, gaps
      - `08-acceptance-criteria.md` — concrete test cases
      - `09-open-questions.md` — decisions to resolve during migration
    - Living reference updated throughout migration.
      Serves as acceptance criteria for Phase 1 (port/main.c)
      and Phase 4 (supervisor lifecycle).

0.1 **Root port files** — qstrdefsport.h, mphalport.h
    - mphalport.h: review all macros, especially thread-related ones
    - qstrdefsport.h: review for any needed additions or removals

0.2–0.3 **Deferred: Build system + board configs**
    - Makefile, mpconfigport.h, mpconfigport.mk, boards/standard/,
      boards/browser/ — these continue to live in `ports/wasm/` and
      are used as-is during development.
    - References to functions/macros that change during subsequent
      phases will be updated incrementally as those phases land.
    - Full build system restructuring (BOARD vs VARIANT, merged
      board configs, CIRCUITPY_* flag audit) is future work to
      prepare for a potential upstream merge if the port were to
      be officially adopted.

**Review gate**: mphalport.h and qstrdefsport.h reviewed. No build
system changes required — we build against `ports/wasm/Makefile`
during migration.

---

### Phase 1: Wasm layer primitives (chassis → port/)

**Goal**: Bring over the chassis-proven code that forms the port's
foundation.  These are the wasm-layer primitives that have no upstream
contract dependencies — they define the memory model and abort-resume
contract that the rest of the wasm layer builds on.

All files in this phase (and Phases 2–5) are part of the **wasm layer**
— see `design/wasm-layer.md`.  The phase split reflects build order
(primitives first, then contract implementations), not separate layers.

**Chunks**:

1.1 **port/constants.h + port/macros.h**
    - From chassis_constants.h + port_macros.h
    - Rename, review every constant

1.2 **port/port_memory.c/h**
    - From chassis/port_memory.c/h
    - Add fields needed by supervisor (from supervisor/port_memory.h)
    - This is the most important file — defines all frame-durable state
    - Audit: every field must have a comment (purpose, lifetime, who writes)

1.3 **port/budget.c/h**
    - From chassis, essentially as-is
    - Small, self-contained

1.4 **port/serial.c/h**
    - Ring buffers from chassis
    - Review: are buffer sizes adequate for REPL + display output?

1.5 **port/hal.c/h**
    - From chassis/hal.c + supervisor/hal.c (pin categories)
    - Consolidate: chassis has direct-memory access, supervisor has
      categories and claim/release — merge both
    - Stub audit: what does the real raspberrypi port do in hal functions
      that we no-op? Document each gap.
    - Part of the "wasm layer" — see design/wasm-layer.md

1.6 **port/vm_abort.c**
    - From chassis, essentially as-is
    - The abort-resume protocol implementation

1.7 **port/port_step.c/h**
    - From chassis, extended with VM phase states from consolidation doc
    - The frame phase state machine (PHASE_BOOT, PHASE_CODE, etc.)
    - Revisit SF_ACTIVE / SF_YIELDED / SF_COMPLETE flags (constants.h):
      with Option A, port_idle_until_interrupt handles yield-during-delay
      internally. Do these flags still accurately describe the states
      the frame loop needs to track?

1.8 **port/event_ring.c/h**
    - From supervisor/semihosting.c (event drain portion)
    - Rewritten to use port_mem directly, not FD reads

1.9 **port/ffi_imports.h + port/ffi_exports.c + port/memfs_imports.h**
    - All WASM import/export declarations in one place
    - Audit: every import and export documented

1.10 **port/main.c**
     - From chassis/main.c, extended with the CP supervisor lifecycle
       (chassis_init, chassis_frame with phase state machine)
     - This is the frame loop — the heart of the port

**Review gate**: `port/` compiles as a unit (with stubs for Layer C
calls). The chassis test suite (test-chassis.mjs) passes against the
new port/ code.

---

### Phase 2: Wasm layer — FFI (ffi/)

**Goal**: Consolidate the JS↔Python proxy files.

**Chunks**:

2.1 **ffi/proxy_c.c/h** — from root proxy_c.c/h
2.2 **ffi/objjsproxy.c** — from root objjsproxy.c
2.3 **ffi/modjsffi.c** — from root modjsffi.c
2.4 **ffi/jsffi_imports.h** — from root jsffi_imports.h

These are largely file moves with namespace cleanup. Each file gets a
layer compliance review (Test 2 from consolidation doc).

**Review gate**: FFI proxy compiles, test-ffi.mjs passes.

**User comment**: Check we are at functional parity with the original [MicroPython](https://github.com/micropython/micropython/tree/master/ports/webassembly) implementation.

---

### Phase 3: Wasm layer — platform HAL (root port files)

**Goal**: Bring over the wasm-layer files that fulfill MicroPython's
`mp_hal_*` and `py/` contracts.

**Chunks**:

3.1 **main.c** (CLI entry point, standard board only)
    - Distinct from port/main.c (frame loop)
    - Review: should this eventually merge with port/main.c behind
      a `#if`? (consolidation doc says no — keep separate)

3.2 **gccollect.c**
    - Review against other ports: are we scanning all root pointers?
    - port_mem regions need to be registered as GC roots

3.3 **wasi_mphal.c → port/wasi_mphal.c**
    - Platform HAL (clock, random, stdio mode, interrupt char)
    - Co-locate with hal.c in port/ — both are "wasm layer" files
      (same substrate, different contracts). See design/wasm-layer.md.
    - **Option A (decided)**: adopt upstream supervisor/shared/ for
      functions it owns. Concrete changes:
      - REMOVE mp_hal_delay_ms override from mpconfigport.h — let
        supervisor/shared/tick.c provide it
      - REMOVE mp_hal_delay_ms implementation from wasi_mphal.c
      - KEEP mp_hal_ticks_ms/us, mp_hal_delay_us, mp_hal_get_random,
        mp_hal_set_interrupt_char (safe to own directly)
      - Evaluate supervisor/shared/micropython.c for console routing
        — can it work with our ring buffers, or do we still need a
        port-specific replacement?

3.4 **mpthreadport.c/h**
    - Critical review per the "stubs and dismissed functionality"
      section: what do the thread functions actually need to do for
      asyncio, for our context scheduler, for mp_thread_init-before-
      mp_init ordering?
    - Compare with unix port's mpthreadport.c

3.5 **board_display.c/h + wasm_framebuffer.c/h**
    - Display plumbing
    - Review: does this belong at root, in port/, or in common-hal/?

3.6 **modmachine.c, modos.c, modringio.c**
    - Module implementations
    - Review: are these complete? Stubs documented?

3.7 **modasyncio.c**
    - Re-evaluate current implementation and decide whether a port-specific
      version is warranted
    - Critical review to ensure our ultimate implementation fully supports
      the exposed CircuitPython `asyncio` module API

**Review gate**: All root files compile with port/ and ffi/.

---

### Phase 4: Wasm layer — supervisor contract (supervisor/)

**Goal**: Bring over the wasm-layer files that fulfill CircuitPython's
`supervisor/port.h` contracts, using Phase 1 primitives.  These files
are the same layer as port/ — they just implement different upstream
contracts (see `design/wasm-layer.md`).

**Chunks**:

4.1 **supervisor/stubs.c**
    - First, because it sets the tone. Every stub function gets the
      full treatment: find caller, read real port implementation, map
      effect to our substrate, implement or document why not.
    - This is the most important review in the entire migration.

4.2 **supervisor/tick.c + supervisor/background_callback.c**
    - Lightweight/heavyweight tick split
    - Review: port_enable_tick / port_disable_tick — what should these
      actually DO on our port? (Not empty.)
    - **Option A critical path**: port_idle_until_interrupt() must
      implement abort-resume yield to JS.  port_get_raw_ticks() must
      return WASI clock_gettime values (1/1024-sec ticks).
      port_interrupt_after_ticks() must store deadline for frame loop.
      These three functions are how the upstream mp_hal_delay_ms
      loop works on our platform.

4.3 **supervisor/compile.c/h**
    - Unified compile service
    - Small, self-contained, essential

4.4 **supervisor/context.c/h**
    - Multi-context scheduling
    - Review: how does this interact with the new phase state machine
      in port/port_step.c?
    - Worth keeping?

4.5 **supervisor/serial.c**
    - Display/console routing
    - Rework to use port/serial.c ring buffers

4.6 **supervisor/port.c**
    - port_init, port_deinit, reset_port
    - Rework to use port/port_memory for memory management

4.7 **supervisor/display.c + supervisor/status_bar.c**
    - Displayio backend
    - Review: dependencies on wasm_framebuffer

4.8 **supervisor/micropython.c → merge into stubs.c** (if still needed)

**Review gate**: supervisor/ compiles with port/. The full VM boots
(chassis_init succeeds, REPL prompt appears).

---

### Phase 5: Wasm layer — common-hal/

**Goal**: Bring over hardware abstraction implementations, updating
each to use hal.c instead of FD-based I/O.  Same wasm layer, fulfilling
CircuitPython's `common-hal/` peripheral contracts.

**Chunks**:

5.1 **common-hal/microcontroller/** — Pin.c/h, Processor.c/h, __init__.c
    - Foundation: pin table, processor info, interrupt enable/disable
    - Review: disable_interrupts / enable_interrupts — implement as
      event ring suppression flag, not empty

5.2 **common-hal/digitalio/** — DigitalInOut.c/h
    - Rework from hal_gpio_fd() + lseek/read/write to hal_write_pin /
      hal_read_pin

5.3 **common-hal/analogio/** — AnalogIn, AnalogOut
5.4 **common-hal/pwmio/** — PWMOut
5.5 **common-hal/busio/** — I2C, SPI, UART
5.6 **common-hal/board/** — board_pins.c (move to boards/{variant}/)
5.7 **common-hal/displayio/**, **neopixel_write/**, **storage/**, **os/**

**Review gate**: `make BOARD=browser` produces a .wasm that boots,
shows REPL, responds to pin toggles.

---

### Phase 6: JS runtime

**Goal**: Consolidate and rename JS files.

**Chunks**:

6.1 **js/wasi.js** — from wasi-memfs.js + chassis/wasi-chassis.js
6.2 **js/board.mjs** — from circuitpython.mjs + chassis-api.mjs
6.3 **js/serial.mjs** — from readline.mjs + shell.mjs
6.4 **js/ffi.js** — from jsffi.js
6.5 **js/display.mjs, js/hardware.mjs, js/env.mjs, js/targets.mjs**
    — renames / minor updates
6.6 **js/constants.mjs** — from chassis-constants.mjs

**Review gate**: Browser test page loads, REPL works end-to-end,
LED toggle updates SVG.

---

### Phase 7: Tests + documentation

**Goal**: Consolidate tests, update all documentation.

**Chunks**:

7.1 **tests/** — consolidate all test files, update import paths
7.2 **design/** — carry over, update every doc to reflect new paths
    and any decisions made during migration
7.3 **CLAUDE.md / MEMORY.md** — update all references
7.4 **README.md** — rewrite for new structure

**Review gate**: All tests pass. All design docs reference correct
paths. No mention of `chassis/` or `variants/` in any file.

---

### Phase 8: The swap

**Goal**: Replace ports/wasm with ports/wasm-tmp.

**Pre-swap checklist**:
- [ ] Every file in wasm-tmp/ has been reviewed
- [ ] All tests pass: test-chassis, test-ffi, test-vm-ready,
      test-abort-resume, browser REPL, LED toggle
- [ ] `make BOARD=standard` builds and passes CLI tests
- [ ] `make BOARD=browser` builds and passes browser tests
- [ ] No file references old paths (chassis/, variants/, old JS names)
- [ ] Design docs are current
- [ ] git diff between old and new is understood — no accidental
      deletions of needed functionality

**Swap procedure**:
1. `git mv ports/wasm ports/wasm-old`
2. `git mv ports/wasm-tmp ports/wasm`
3. Full rebuild + test
4. If clean: commit, move ports/wasm-old to home/jef/dev/archive/
5. If not: investigate, fix, re-test

---

## Design traceability

New C files MUST include header comments that reference the design
documents governing the code's behavior.  This is especially important
in early phases where we are proving the design implementation.

Example:

```c
// port/boot.c — boot.py execution and VM teardown
//
// Design refs:
//   design/behavior/04-script-execution.md  (boot.py sequence)
//   design/behavior/05-vm-lifecycle.md      (cleanup_after_vm)
//   design/behavior/09-open-questions.md    (Q6: boot.py isolation)
```

Functions that implement specific documented behaviors should include
a brief inline reference:

```c
// Per 04-script-execution.md: boot.py runs in its own VM instance.
// Supervisor-level state persists; Python globals do not.
```

This traceability serves two purposes:
1. **Confidence**: reviewers can cross-check implementation against
   design intent without hunting for the relevant doc.
2. **Evolution**: when a design decision changes, grep for the doc
   reference to find all affected code.

As the codebase stabilizes, these references can be thinned — but
during migration they are mandatory.

## Chunk review protocol

For each chunk delivered:

1. Files are written to wasm-tmp/ with clear comments marking:
   - **Origin**: where this code came from (chassis/X, supervisor/Y, etc.)
   - **Changes**: what changed from the original and why
   - **Stubs**: every stub has a WHY comment
   - **TODO**: anything that depends on a later phase
   - **Design refs**: which behavior/ or design/ docs govern this code

2. User reviews, adds inline comments/questions.

3. We address comments before proceeding to the next chunk.

4. When a chunk is approved, it's marked complete in this plan.

## Progress tracking

| Phase | Chunk | Status | Notes |
|-------|-------|--------|-------|
| 0.0   | Behavior guide | **done** | design/behavior/ — lifecycle, runtime envs, deviations |
| 0.1   | Root port files | **done** | mphalport.h, qstrdefsport.h + wasm-layer discovery |
| 0.2–3 | Build system + config | **done** | Makefile, mpconfigport.h (Option A), variants/; `make VARIANT=standard` builds 1.1M .wasm |
| 1.1   | constants + macros | **done** | GPIO_MAX_PINS=32, SF_* flags revisited in 1.7 |
| 1.2   | port_memory | **done** | merged chassis + supervisor, dropped port_stack_t |
| 1.3   | budget | **done** | as-is from chassis |
| 1.4   | serial | **done** | as-is from chassis, ring buffers in port_mem |
| 1.5   | hal | **done** | merged chassis + supervisor, direct memory, no FDs |
| 1.6   | vm_abort | **done** | +port_idle_until_interrupt (Option A), dropped mp_hal_delay_ms override |
| 1.7   | port_step | **done** | lifecycle phases replace port_stack_t, SF_* resolved |
| 1.8   | event_ring | **done** | from semihosting, uses port_mem ring, weak dispatch |
| 1.9   | ffi decls | **done** | consolidated imports + exports + memfs |
| 1.10  | port/main.c | **done** | port_init + port_frame, nlr_set_abort stub for Phase 4 |
| 2.1-4 | ffi/ | **done** | file moves + namespace cleanup, no code changes |
| 3.1   | main.c (CLI) | **done** | carried forward, TODO notes for cleanup |
| 3.2   | gccollect.c | **done** | context GC stub for Phase 4.4 |
| 3.3   | wasi_mphal.c | **done** | Option A: port_get_raw_ticks, no mp_hal_delay_ms |
| 3.4   | mpthreadport | **done** | carried forward as-is |
| 3.5   | display files | **done** | board_display + wasm_framebuffer carried forward |
| 3.6   | module files | **done** | modmachine, modos, modringio carried forward |
| 3.7   | modasyncio | **deferred** | uses extmod/ + frozen module; validate at build/test time that mp_event_wait_ms → wasm_wfe → abort-resume works for asyncio sleep/yield |
| 4.1   | stubs.c | **done** | full WHO/WHAT/WHY docs for every stub |
| 4.2   | tick + background_callback | **done** | removed semihosting dep |
| 4.3   | compile.c/h | **done** | carried forward |
| 4.4   | context.c/h | **deferred** | tied to SUSPEND; bring in for multi-context |
| 4.5   | serial.c | **done** | updated include path, TODO for ring buffer rework |
| 4.6   | port.c | **done** | split: timing→wasi_mphal, idle→vm_abort, rest here |
| 4.7   | display + status_bar | **done** | carried forward |
| 4.8   | micropython.c | **done** | updated port_memory include path |
| 5.1   | microcontroller/ | **done** | 32 pins, gpio_slot() access, port/hal.h |
| 5.2   | digitalio/ | **done** | FD→direct memory rework, pin listeners kept |
| 5.3   | analogio/ | **done** | FD→analog_slot() direct memory |
| 5.4   | pwmio/ | **done** | FD→direct memory, duty/freq in object struct |
| 5.5   | busio/ | **done** | carried forward (I2C/SPI/UART) |
| 5.6   | board/ | **done** | 32 pins, port/hal.h |
| 5.7   | displayio/neopixel/storage/os | **done** | carried forward + neopixel FD rework |
| 6.1-6 | js/ runtime | **done** | 14 files: 12 carried + constants.mjs + compat.mjs shim; imports updated, reactor model |
| 7.1-4 | tests + docs | **done** | tests/ with make test, README.md, design docs current |
| 8     | swap | pending | |
