# Port Chassis: MEMFS-in-Linear-Memory

**Status**: Design direction (2026-04-25)
**Goal**: Build a standalone "port chassis" program — compiled with wasi-libc,
no MicroPython dependency — that proves the MEMFS-in-linear-memory model and
becomes the heart of the CircuitPython WASM port.

## Core thesis

If MEMFS allocates file storage in WASM linear memory (not JS heap), then
C pointers, JS `Uint8Array` views, and Python `machine.mem32` all see the same
bytes.  Halting execution is just `return` — all state survives in linear
memory.  No unwinding, no re-entrancy, no SUSPEND machinery for state
preservation.

This is the WASM equivalent of ARM's execute-in-place (XIP): the named storage
IS the execution memory.  `PLACE_IN_DTCM` on ARM copies data from flash to
fast SRAM at boot.  Here, MEMFS files ARE linear memory regions — no copy
step needed.

## What this program is

A C program that:

1. **Owns all memory** via a static `port_memory_t`-style struct in linear
   memory.  Every region is also a MEMFS file (the file IS the memory).

2. **Manages a HAL** — knows when pins/buses/components are claimed, enabled,
   or changed.  Hardware state lives in MEMFS regions at known addresses.

3. **Runs a frame loop** — `wasm_frame(now, budget)` called by JS via rAF.
   Soft deadline ~8ms, firm deadline ~10ms.  Returns a status code.  All
   state is in linear memory, so returning is free.

4. **Provides C/JS FFI** — WASM imports for JS→C events (hardware changes,
   input), exported pointers for JS→C state reads, and a foundation for
   Python↔JS object bridging.

5. **Has a heap-resident "C stack"** — a state machine stack allocated in
   linear memory (like pystack for the port itself).  The port's execution
   state is a data structure, not C call frames.  Halt = save position.
   Resume = read position.

No MicroPython, no VM, no GC.  Just the port chassis: memory layout, HAL,
frame loop, FFI.  The VM bolts on later.

## Memory model

### MEMFS-in-linear-memory

Current `wasi-memfs.js`:
```js
createFile(path) {
    this.files.set(path, { data: new Uint8Array(0) });  // JS heap
}
```

Proposed:
```js
createFile(path, ptr, size) {
    this.files.set(path, {
        ptr,
        data: new Uint8Array(wasm.exports.memory.buffer, ptr, size)
    });
}
```

The `Uint8Array` view is zero-copy — a typed array view over the WASM memory
buffer.  C writes to `ptr`, JS reads from `data`, same bytes.

C side registers regions at init:
```c
// memfs_register: tell MEMFS "this C memory IS this file"
// Implemented as a WASM import that JS handles
__attribute__((import_module("memfs"), import_name("register")))
void memfs_register(const char *path, void *ptr, uint32_t size);
```

### Memory layout

```
port_memory_t (static, in .bss):

  Offset   Size      MEMFS path              Purpose
  ──────   ────      ──────────              ───────
  0x0000   64        /port/state             Port state machine
  0x0040   256       /port/stack             Port execution stack
  0x0140   512       /port/event_ring        JS→C event ring
  0x0340   64×12     /hal/gpio               GPIO pin slots (12B each)
  0x0640   64×4      /hal/analog             ADC values
  0x0740   64×8      /hal/pwm               PWM state
  0x0B40   1024      /hal/neopixel           Pixel data
  0x0F40   4096      /hal/serial/rx          Serial input buffer
  0x1F40   4096      /hal/serial/tx          Serial output buffer
  0x2F40   ...       (reserved for VM)       GC heap, pystacks, etc.
```

Every row is both a C struct field and a MEMFS file.  JS reads/writes via
`memfs.readFile('/hal/gpio')`.  C reads/writes via pointer.  Same bytes.

### PLACE_IN_*TCM

On ARM, `PLACE_IN_DTCM` tells the linker "put this in the DTCM section" and
the startup code copies it from flash to fast SRAM.

On this port, `PLACE_IN_DTCM` means "this data lives in a MEMFS-registered
region."  No copy — the MEMFS file IS the linear memory.  But the macro
serves the same purpose: marking data that must be at a known, stable address
for both C and external access.

```c
#define PLACE_IN_DTCM  __attribute__((section(".memfs")))
// Linker script collects .memfs sections; port registers them with MEMFS
```

## Frame loop

```c
// Single entry point, called once per rAF from JS
uint32_t wasm_frame(double now_ms, double budget_ms) {
    port->now_us = (uint64_t)(now_ms * 1000);
    port->budget_us = (uint32_t)(budget_ms * 1000);
    port->frame_count++;

    // Phase 1: Drain event ring — process JS→C events
    //   Hardware changes, keyboard input, timer fires
    //   Events arrive as records in /port/event_ring
    //   State is written directly to /hal/* by JS before the event
    //   State updates are via exported FFI events both JS and wasm see
    //   The event is notification only ("pin 5 changed")
    port_drain_events();

    // Phase 2: HAL step — process dirty flags, wake dependents
    //   Check which pins/buses changed, update claim state
    //   Schedule background work if needed
    hal_step();

    // Phase 3: Run port logic — state machine step
    //   Reads/writes port->stack (the heap-resident "C stack")
    //   Can halt at any point by saving position and returning
    //   Budget check: if elapsed > soft_budget, stop and return
    int rc = port_step();

    // Phase 4: Export state — write status for JS to read
    //   JS reads /port/state directly from linear memory
    port_export_state(rc);

    return rc;
}
```

No deep C call chain.  `port_step()` is a state machine that reads its
position from `port->stack`, does work, writes position back.  Returning
is free — nothing to unwind.

## HAL design

### Hardware update flow

Three options explored.  The hybrid (option 2) fits best:

**Option 2: JS writes state directly + event notification**

1. JS writes hardware state directly to MEMFS regions in linear memory
   (e.g., write pin 5 value to `/hal/gpio` offset `5*12+2`)
2. JS pushes a lightweight event to `/port/event_ring`: "pin 5 changed"
3. C drains events, reads the new state from the MEMFS region (it's already
   there — JS wrote it), sets dirty flags
4. `hal_step()` checks dirty flags, wakes dependents

Why this works:
- State is always consistent (JS writes atomically before notifying)
- No parsing — C reads structured data at known offsets
- Events are small (type + pin index) — the ring stays tiny
- Polling is cheap — just check dirty bitmasks
- Same model as the current `SH_EVT_HW_CHANGE` + `/hal/gpio` design

### Claim awareness

The port knows what's claimed because claim state is in the MEMFS slot:

```c
// hal_step checks all slots with HAL_FLAG_JS_WROTE
uint64_t dirty = port->hal_gpio_dirty;
while (dirty) {
    int pin = __builtin_ctzll(dirty);
    dirty &= dirty - 1;

    uint8_t *slot = &port->hal_gpio[pin * HAL_GPIO_SLOT_SIZE];
    uint8_t role = slot[HAL_GPIO_OFF_ROLE];
    uint8_t flags = slot[HAL_GPIO_OFF_FLAGS];

    // Port knows: pin 5 is claimed as DIGITAL_IN, JS wrote a new value
    if (role != HAL_ROLE_UNCLAIMED && (flags & HAL_FLAG_JS_WROTE)) {
        // Wake any dependent (interrupt handler, asyncio waiter, etc.)
        port_wake_pin(pin);
    }
}
```

## FFI unification

### machine.mem32 = direct memory access

If all state is in MEMFS-backed linear memory, `machine.mem32[addr]` is a
pointer dereference.  The port validates the address falls in a known region:

```c
uintptr_t mod_machine_mem_get_addr(mp_obj_t addr_o, uint align) {
    uintptr_t addr = mp_obj_get_int_truncated(addr_o);
    // Validate: must be in a registered MEMFS region
    if (addr >= PORT_MEM_BASE && addr < PORT_MEM_BASE + PORT_MEM_SIZE) {
        return addr;  // It's linear memory — the address IS the pointer
    }
    mp_raise_ValueError("address not in mapped region");
}
```

### jsffi = JS object access

`jsffi` remains for JS objects with no memory representation (DOM, Promises,
fetch).  But for hardware state — the hot path — `machine.mem8/16/32` is
zero-overhead.  The two modules complement:

- `machine.mem32[GPIO_BASE + pin*12]` — read pin state (pointer deref)
- `jsffi.global_this.document` — access DOM (proxy + PVN marshal)

### "All Python objects can be JS objects"

If the GC heap is a MEMFS region, JS can read Python object headers and data
directly from linear memory.  And JS can construct Python objects by writing
the correct bytes.  The FFI becomes shared memory with a known schema.

This is future work, but the MEMFS-in-linear-memory model is the prerequisite.
The port chassis doesn't need this — it's a foundation for later.

## Halt/resume model

### Why it's free

All port state lives in `port_memory_t` (linear memory).  The C stack holds
only local variables for `wasm_frame()` and its callees.  When `wasm_frame()`
returns:

- C stack frames evaporate (they're function-local)
- `port_memory_t` persists (it's in .bss, not stack)
- MEMFS files persist (they're views of the same memory)
- JS reads output from linear memory
- Next frame: JS calls `wasm_frame()` again, C reads from same memory

No unwinding.  No setjmp/longjmp.  No NLR.  No sentinel exceptions.
Just `return`.

### The heap-resident "C stack"

The port's execution state is a state machine, not nested C calls:

```c
typedef struct {
    uint32_t phase;        // PORT_PHASE_INIT, _HAL, _VM, _EXPORT, ...
    uint32_t sub_phase;    // within-phase position
    uint32_t data[56];     // scratch space for in-progress work
} port_stack_t;

int port_step(void) {
    port_stack_t *stk = &port->stack;

    switch (stk->phase) {
    case PORT_PHASE_INIT:
        // one-time initialization
        stk->phase = PORT_PHASE_HAL;
        return PORT_CONTINUE;

    case PORT_PHASE_HAL:
        hal_step();
        stk->phase = PORT_PHASE_VM;
        return PORT_CONTINUE;

    case PORT_PHASE_VM:
        // Future: call into MicroPython VM here
        // Budget check: if over soft deadline, save position and return
        if (port_elapsed_us() > port->soft_budget_us) {
            return PORT_YIELD;  // will resume at PORT_PHASE_VM next frame
        }
        stk->phase = PORT_PHASE_EXPORT;
        return PORT_CONTINUE;

    case PORT_PHASE_EXPORT:
        port_export_state(PORT_DONE);
        stk->phase = PORT_PHASE_HAL;  // reset for next frame
        return PORT_DONE;
    }
    return PORT_ERROR;
}
```

The key: `port_step()` returns after each phase.  If budget is exhausted
mid-phase, it saves its position in `stk` and returns.  Next call picks up
where it left off.  The C stack is empty between calls.

## Implementation plan

### Phase 1: Memory substrate (the foundation)

**Goal**: MEMFS-in-linear-memory works.  C writes, JS reads, same bytes.

1. Define `port_memory_t` struct with all regions (HAL, event ring, state)
2. Add `memfs_register(path, ptr, size)` WASM import
3. Modify `wasi-memfs.js` to support "aliased" files backed by WASM memory
4. At init, C calls `memfs_register` for each region
5. Verify: C writes to struct field, JS reads via memfs API, values match

**Files**:
- `chassis/port_memory.h` — memory layout struct
- `chassis/port_memory.c` — static instance + registration
- `chassis/memfs_imports.h` — WASM import declarations
- `js/wasi-memfs.js` — add aliased file support

### Phase 2: Frame loop + HAL

**Goal**: `wasm_frame()` runs per rAF, processes events, updates HAL state.

1. Implement event ring (JS pushes events, C drains)
2. Implement `hal_step()` with dirty flag scanning
3. Implement `wasm_frame()` with budget tracking
4. JS harness: rAF loop, hardware simulation UI (buttons, sliders)
5. Verify: click button in UI → event → hal_step sees change → state updated

**Files**:
- `chassis/event_ring.c` — ring buffer drain
- `chassis/hal.c` — dirty flags, claim tracking, pin wake
- `chassis/frame.c` — wasm_frame entry point
- `js/chassis-harness.js` — test harness

### Phase 3: Port state machine

**Goal**: Port logic runs as a resumable state machine, halt/resume is free.

1. Implement `port_stack_t` and `port_step()` state machine
2. Add budget checking — yield mid-work, resume next frame
3. Add `PLACE_IN_DTCM` macro that registers with MEMFS via linker section
4. Verify: port does multi-frame work, halts and resumes cleanly

**Files**:
- `chassis/port_step.c` — state machine
- `chassis/port_macros.h` — PLACE_IN_DTCM, memory region helpers

### Phase 4: FFI layer

**Goal**: C/JS communication works both directions without fd round-trips.

1. Port WASM import declarations (event push, state read, pin listener)
2. Port WASM export declarations (wasm_frame, init, memory region queries)
3. Shared constants header (event types, HAL offsets, status codes)
4. Verify: JS pushes events, C processes them, JS reads results — all via
   linear memory, no WASI fd calls in the hot path

**Files**:
- `chassis/ffi_imports.h` — WASM imports (JS functions C can call)
- `chassis/ffi_exports.c` — WASM exports (C functions JS can call)
- `js/chassis-ffi.js` — JS side of FFI

### Phase 5: VM preparation

**Goal**: The chassis is ready for MicroPython to bolt on.

1. Reserve GC heap and pystack regions in `port_memory_t`
2. Register them as MEMFS files
3. Add `machine.mem32` address validation for all regions
4. Stub out `port_step()` VM phase — calls mp_execute_bytecode when ready
5. Document the contract: what the VM needs from the chassis, what the
   chassis provides

This phase doesn't add MicroPython — it prepares the slot.

## Relationship to current port

This chassis is the future `ports/wasm/` port core.  The current port has:

- `supervisor/supervisor.c` — becomes `chassis/frame.c` + `chassis/port_step.c`
- `supervisor/port_memory.h` — becomes `chassis/port_memory.h` (MEMFS-registered)
- `supervisor/hal.h` — becomes `chassis/hal.c` (same concepts, MEMFS-backed)
- `supervisor/semihosting.h` — becomes `chassis/event_ring.c` + `chassis/ffi_imports.h`
- `js/wasi-memfs.js` — gains aliased file support
- `js/semihosting.js` — absorbed into `js/chassis-ffi.js`
- `modmachine.c` — `mem_get_addr` gains region validation
- `modjsffi.c` — unchanged (still handles JS object proxies)

The current code is the reference.  The chassis is the clean-room rebuild of
the same ideas with MEMFS-in-linear-memory as the unifying principle.

## Open questions

1. **Where to build?**  `ports/wasm/chassis/` as a subdirectory?  Or a
   separate `poc/` directory?  The chassis is intended to become the real
   port, so probably `ports/wasm/chassis/`.

2. **wasi-memfs.js aliased files**: When WASM memory grows (`memory.grow`),
   all existing `Uint8Array` views are detached.  The MEMFS must re-create
   views after growth.  This is solvable (re-derive from `memory.buffer`)
   but needs explicit handling.

   **User comment** Perhaps consider over-allocating WASM memory (maybe 2x the current amount) just so growth or split GC heaps can be handled 'in house'?

3. **Event delivery model**: Events-only vs state-write+event-notify vs
   pure polling.  The hybrid (state in memory, events as notifications)
   seems right but needs validation.

4. **How much port logic needs the state machine stack?**  If `port_step()`
   phases are short enough to always complete within budget, the stack is
   unnecessary — just a phase counter suffices.  The stack is insurance for
   future complex port logic (boot sequences, multi-step init).

   **User comment** The stack is less for the port than the VM, eventually. Some things may need to be 'on stack' and we need the port to be able to halt without the burden of saving anything off.
