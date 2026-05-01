# Abort-Resume: VM Halt/Resume via `nlr_jump_abort`

**Status:** Design — ready to test
**Date:** 2026-04-26
**Supersedes:** SUSPEND mechanism (sentinel, NLR identity checks, thread bookends)

## Problem

The WASM port must return control to JS periodically (frame budget). On every
other platform, the C stack persists across yields — the OS preserves it
(Unix threads block on a mutex; ARM halts at WFI). On WASM, the C stack is
destroyed when we return to JS.

MicroPython never saves `ip`/`sp` mid-execution because it never needs to —
the C stack is always there. We are the only port where it isn't.

## Design

### Core idea

Use MicroPython's existing `vm_abort` mechanism. The chassis sets itself as
the abort landing point. Budget expiry triggers abort. The VM teleports back
to the chassis. Two extra pointer saves in `HOOK_LOOP` make resume safe.

**Zero upstream code changes.**

### Memory layout

All VM state lives in `port_mem`, which is MEMFS-registered linear memory:

```
port_memory_t port_mem = {
    .state_ctx    = ...,   // mp_state_ctx_t — the entire VM universe
    .gc_heap      = ...,   // GC-managed Python objects
    .pystack      = ...,   // code_state chain (all bytecode frames)
    ...
};
```

`mp_state_ctx` is placed in `port_mem` via the `PLACE_IN_DTCM_BSS` macro
(which the port redefines) or by making `mp_state_ctx` a macro that resolves
to `port_mem.state_ctx`.

When the chassis returns to JS, everything is frozen in linear memory.
Nothing moves. Nothing is freed. The WASM memory buffer persists.

### The two-store fix

```c
#define MICROPY_VM_HOOK_LOOP \
    code_state->ip = ip; \
    code_state->sp = sp; \
    wasm_vm_hook_loop(code_state);
```

Two pointer writes per backwards branch. This is the entire cost of making
abort-and-resume safe. `ip` and `sp` are C locals that would normally be lost
when `nlr_jump_abort` destroys the C stack. By writing them to `code_state`
(which lives on pystack, in `port_mem`), they survive.

### Chassis frame

```c
uint32_t chassis_frame(double budget_ms) {
    _budget_ms = budget_ms;
    _frame_start = now_ms();

    nlr_buf_t nlr;
    nlr_set_abort(&nlr);          // chassis IS the abort boundary

    if (nlr_push(&nlr) == 0) {
        mp_execute_bytecode(code_state);
        nlr_pop();
        // Normal completion
    } else {
        // nlr.ret_val == NULL → abort (budget or WFE)
        // nlr.ret_val != NULL → unhandled exception
    }

    nlr_set_abort(NULL);
    return 0;
}
```

### Budget enforcement (preemptive backstop)

```c
void wasm_vm_hook_loop(mp_code_state_t *cs) {
    serial_check_interrupt();

    double elapsed = now_ms() - _frame_start;
    if (elapsed >= _budget_ms) {
        mp_sched_vm_abort();
        // Next mp_handle_pending() → nlr_jump_abort() → chassis
    }
}
```

### Cooperative yield (WFE)

When the VM has nothing to do (asyncio sleep, time.sleep, blocking I/O),
it calls `mp_event_wait_ms()` → `MICROPY_INTERNAL_WFE`:

```c
#define MICROPY_INTERNAL_WFE(timeout_ms) wasm_wfe(timeout_ms)

void wasm_wfe(int timeout_ms) {
    // VM says "I'm idle for timeout_ms"
    // Store wakeup deadline for JS to read
    port_mem.wakeup_ms = (timeout_ms < 0) ? 0 : now_ms() + timeout_ms;
    mp_sched_vm_abort();
    // → nlr_jump_abort() → chassis → return to JS
}
```

JS reads `port_mem.wakeup_ms` and schedules the next `chassis_frame` call
accordingly (setTimeout or next rAF, whichever is sooner).

### Abort path (what happens)

`mp_sched_vm_abort()` sets `vm_abort = true`. At the next backwards branch:

1. `MICROPY_VM_HOOK_LOOP` saves `ip`/`sp` to `code_state`
2. VM checks `mp_handle_pending(true)` (already in the hot path)
3. `mp_handle_pending` sees `vm_abort == true`
4. Calls `nlr_jump_abort()`:
   - Replaces `nlr_top` with `nlr_abort` (the chassis buffer)
   - Runs `nlr_call_jump_callbacks()` (cleanup handlers)
   - Restores `pystack_cur` via `MP_NLR_RESTORE_PYSTACK`
   - `longjmp` to chassis

### Resume

Next frame, the chassis calls `mp_execute_bytecode(code_state)`.
`code_state->ip` and `code_state->sp` were saved by `HOOK_LOOP`.
The VM resumes from exactly where it left off.

**Note on pystack_cur:** `nlr_jump_abort` restores `pystack_cur` to the
level saved in the chassis's `nlr_buf_t`. If the VM allocated code_states
on pystack between chassis entry and abort, those allocations are "freed"
(pystack_cur moved back). But the data is still in linear memory. On resume,
`mp_execute_bytecode` will re-use the existing code_state passed to it —
it doesn't allocate a new one. Inner function calls will re-allocate on
pystack as needed.

### Two paths, same destination

| Scenario | Trigger | Mechanism |
|---|---|---|
| Budget expired | `HOOK_LOOP` | `mp_sched_vm_abort()` (preemptive) |
| asyncio sleep | `time.sleep` → `WFE` | `mp_sched_vm_abort()` (cooperative) |
| Blocking I/O | `mp_event_wait_ms` → `WFE` | `mp_sched_vm_abort()` (cooperative) |
| Ctrl-C | serial interrupt | `mp_sched_keyboard_interrupt()` → exception |
| `while True: pass` | `HOOK_LOOP` | `mp_sched_vm_abort()` (preemptive) |

### Boot sequence

Mirrors Unix port ordering — thread/TLS setup wraps everything:

```
chassis_init():
    port_mem_init()                    // allocate port_mem regions
    mp_thread_init()                   // bind mp_state_ctx.thread (our TLS equivalent)
    mp_cstack_init()                   // capture stack top
    gc_init(port_mem.gc_heap, ...)     // GC tables over port_mem region
    mp_pystack_init(port_mem.pystack)  // pystack over port_mem region
    mp_init()                          // initialize mp_state_ctx internals
```

### What we remove

- `MP_VM_RETURN_SUSPEND` — not needed
- `mp_vm_suspend_sentinel` — not needed
- `MICROPY_VM_YIELD_ENABLED` — not needed
- Identity checks in `vm.c` exception handler — not needed
- `fun_bc_call` SUSPEND handling — not needed
- `objgenerator.c` SUSPEND handling — not needed
- `vm_yield.c` yield machinery — replaced by abort
- Thread bookend design — not needed

### What we keep from existing infrastructure

- `MICROPY_ENABLE_VM_ABORT` (already in MicroPython)
- `mp_sched_vm_abort()` / `nlr_set_abort()` / `nlr_jump_abort()`
- `MICROPY_VM_HOOK_LOOP` / `MICROPY_VM_HOOK_RETURN`
- `MICROPY_INTERNAL_WFE`
- `mp_handle_pending()` at every backwards branch
- `nlr_call_jump_callbacks()` for cleanup
- `mp_sched_keyboard_interrupt()` for Ctrl-C
- asyncio cooperative scheduling via `time.sleep` → `WFE`

### Risks to test

1. **pystack_cur after abort:** Does the NLR restore leave pystack in a state
   where `mp_execute_bytecode(code_state)` can resume? The code_state data
   is still in memory, but pystack_cur was rewound. Need to verify that
   re-entering doesn't corrupt.

2. **Cleanup callbacks:** Do any `nlr_jump_callback` nodes leave state
   inconsistent for resume? (e.g., locks held, GC state)

3. **Nested C calls:** If abort fires while inside a C function that called
   back into Python (rare with stackless), the outer C function's stack frame
   is lost. The code_state for the inner Python function is saved, but the
   C function's local state is not. Need to verify this doesn't happen in
   practice (asyncio + common-hal don't trigger this path).

4. **GC during abort:** If GC was in progress when abort fires, is the GC
   state consistent? (`gc_lock_depth` is in `mp_state_thread_t`, not C stack.)
