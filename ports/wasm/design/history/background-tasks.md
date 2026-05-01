# Background Task Machinery — Alignment Plan

## Status quo

### Upstream model (real boards)

```
SysTick ISR (1ms)
  supervisor_tick()  -->  enqueues supervisor_background_tick as callback

VM backwards branch
  MICROPY_VM_HOOK_LOOP = RUN_BACKGROUND_TASKS
    background_callback_run_all()
      1. port_background_task()          <-- always, be cheap
      2. drain callback queue
           supervisor_background_tick()   <-- runs when ISR queued it
             displayio_background()
             port_background_tick()       <-- port's ~1ms work

VM function return
  MICROPY_VM_HOOK_RETURN = RUN_BACKGROUND_TASKS   (same chain)
```

Key design: ISR *queues* work, VM hooks *drain* it.  Two port entry points
with different cadences:
- `port_background_task()` — every drain, very frequent, must be cheap
- `port_background_tick()` — ~1ms, gated by ISR enqueue of tick callback

### WASM port today

```
cp_hw_step() — once per rAF (~16ms)
  hal_step()                    drain events, latch inputs
  RUN_BACKGROUND_TASKS
    background_callback_run_all()
      port_background_task()    calls supervisor_tick() unconditionally
        supervisor_tick()       enqueues supervisor_background_tick
      drain queue
        supervisor_background_tick()
          displayio_background()
          port_background_tick()   no-op
  hal_export_dirty()

VM backwards branch
  MICROPY_VM_HOOK_LOOP = wasm_vm_hook_loop()
    wasm_background_tasks()     just serial_check_interrupt()
    line number extraction
    budget check

VM function return
  MICROPY_VM_HOOK_RETURN = (empty)   <-- divergence
```

### Problems

1. **`wasm_background_tasks()` is a dead-end name and location.**
   It's the port's in-VM work, but it's defined in supervisor.c with a
   "Future:" comment and isn't connected to the background callback system.
   It duplicates the role of `port_background_task()`.

2. **`port_background_task()` blindly calls `supervisor_tick()` every time.**
   Upstream, `supervisor_tick()` is ISR-gated (~1ms).  Here it runs every
   `background_callback_run_all()` call.  Today that's once per frame (fine),
   but if callbacks are ever drained more frequently, the tick runs too often.

3. **`supervisor_tick()` enqueues → same drain dequeues.**
   Without ISRs, the callback-queue indirection is a round-trip to nowhere:
   `port_background_task()` calls `supervisor_tick()` which enqueues
   `supervisor_background_tick`, which the same `background_callback_run_all()`
   call immediately dequeues and runs.  The queue adds complexity without
   decoupling anything.

4. **`MICROPY_VM_HOOK_RETURN` is empty.**
   Upstream sets it to `RUN_BACKGROUND_TASKS`.  Missing it means callbacks
   registered during a function call won't drain until the next backwards
   branch or next frame.  Probably harmless today but diverges from the
   upstream contract.

5. **Three empty port_*_tick stubs** exist only to satisfy the upstream API.
   They'll never do anything on WASM.

6. **No background work during VM execution.**
   Between `cp_hw_step()` calls (~16ms), the only work at backwards branches
   is Ctrl-C checking.  Display, hardware state, and callbacks don't run.
   A long-running Python loop (even within a single 13ms budget) gets no
   display refresh or hardware polling.

---

## Plan

### Principle

Match the upstream *contract* (what functions exist, when they're called,
what ordering is guaranteed) without mimicking the *mechanism* (ISR + queue
decoupling that only makes sense with real interrupts).

The port's hardware doesn't run by itself — all input arrives from JS
between frames.  So:
- **Port layer** (`cp_hw_step`): JS-to-C data transfer, once per frame
- **Supervisor layer** (`background_callback_run_all`): callback drain +
  periodic work (display, etc.)
- **VM layer** (`wasm_vm_hook_loop`): Ctrl-C, budget, lightweight polling

### Step 1: Wire `MICROPY_VM_HOOK_RETURN`

In `mpconfigport.h`, add:

```c
#define MICROPY_VM_HOOK_RETURN  RUN_BACKGROUND_TASKS;
```

This matches the upstream contract: background callbacks drain on function
returns as well as backwards branches.  Cost: one `background_callback_run_all()`
call per function return.  `port_background_task()` must be cheap (Step 2
ensures this).

**Why:** Callbacks registered during a Python function call (e.g., by
displayio or a C extension) should drain promptly.  The upstream model
guarantees this.

### Step 2: Time-gate `port_background_task()`

Replace the unconditional `supervisor_tick()` call with a time check:

```c
// port.c
static uint64_t _last_tick_ticks = 0;

void port_background_task(void) {
    uint64_t now = port_get_raw_ticks(NULL);
    if (now != _last_tick_ticks) {
        _last_tick_ticks = now;
        supervisor_tick();
    }
}
```

`port_get_raw_ticks()` returns 1/1024s resolution (~0.977ms), so
`supervisor_tick()` runs at most once per ~1ms — matching the upstream
ISR cadence.  Between ticks, `port_background_task()` is a cheap
compare-and-return.

**Why:** With `MICROPY_VM_HOOK_RETURN` wired, `background_callback_run_all()`
runs much more often.  The tick must be gated so `displayio_background()` etc.
don't run at every function return.

### Step 3: Inline `supervisor_background_tick` — eliminate the queue round-trip

Since there's no ISR, `supervisor_tick()` doesn't need to enqueue a callback
for later draining.  It can call `supervisor_background_tick()` directly:

```c
// tick.c
void supervisor_tick(void) {
    // No ISR decoupling needed — call directly
    supervisor_background_tick();
}

static void supervisor_background_tick(void) {
    #if CIRCUITPY_DISPLAYIO
    displayio_background();
    #endif

    last_finished_tick = port_get_raw_ticks(NULL);
}
```

Remove: `tick_callback` static, `background_callback_add` call in
`supervisor_tick()`, `port_start/finish_background_tick()` calls,
`port_background_tick()` call (it's a no-op).

**Why:** The callback queue exists to decouple ISR context (can't call
displayio) from main context (can).  WASM has no ISR — everything runs in
main context.  The queue round-trip adds complexity without providing
decoupling.

**Keep:** `supervisor_enable/disable_tick()` API and `tick_enable_count` —
upstream code calls these (e.g., `pulseio`, `countio`) and the refcount
pattern costs nothing.  Just make them no-ops internally.

### Step 4: Eliminate `wasm_background_tasks()`

Its only job is `serial_check_interrupt()`.  Move that into
`wasm_vm_hook_loop()` directly:

```c
// vm_yield.c
void wasm_vm_hook_loop(const void *code_state_ptr) {
    serial_check_interrupt();
    // ... line extraction ...
    // ... budget check ...
}
```

Remove `wasm_background_tasks()` from supervisor.c.

**Why:** The function name suggests it's the port's background task entry
point (confusable with `port_background_task()`).  It's a single function
call with a "Future:" comment that hasn't materialized.  Inlining removes
the indirection and the confusing name.

### Step 5: Consider `RUN_BACKGROUND_TASKS` in `wasm_vm_hook_loop`

**Don't do this.**  The upstream model puts `RUN_BACKGROUND_TASKS` at every
backwards branch because ISRs can queue work at any time.  On WASM, no work
is queued during VM execution (JS can't call into WASM while the VM is
running).  Running the full drain at every branch would:
- Call `port_background_task()` (time-gated, cheap but still a clock read)
- Drain an always-empty callback queue

Cost without benefit.  The VM hook should stay lean: Ctrl-C + budget.

**Exception:** If we ever add mid-frame hardware polling (e.g., reading
a sensor from an I2C device registry during VM execution), that would go
in `port_background_task()` and we'd want `RUN_BACKGROUND_TASKS` in the
hook.  Cross that bridge when we come to it.

### Step 6 (optional): Rename for clarity

Consider renaming to make the three-layer model explicit:

| Current | Proposed | Role |
|---------|----------|------|
| `cp_hw_step()` | (keep) | Port layer: JS-to-C sync |
| `wasm_vm_hook_loop()` | (keep) | VM layer: lean per-branch work |
| `port_background_task()` | (keep — it's the upstream API name) | Supervisor→Port: time-gated tick |
| `wasm_background_tasks()` | (delete) | Was: orphaned indirection |
| `supervisor_background_tick()` | (keep as static in tick.c) | Tick work: displayio etc. |

No rename needed — deleting `wasm_background_tasks()` is sufficient.

---

## File changes summary

| File | Change |
|------|--------|
| `mpconfigport.h` | Add `MICROPY_VM_HOOK_RETURN` |
| `supervisor/port.c` | Time-gate `port_background_task()` |
| `supervisor/tick.c` | Inline `supervisor_background_tick`, remove callback indirection, remove `port_*_tick()` calls |
| `supervisor/vm_yield.c` | Inline `serial_check_interrupt()`, remove `wasm_background_tasks` extern |
| `supervisor/supervisor.c` | Delete `wasm_background_tasks()` |

## Ordering after changes

```
cp_hw_step() — once per rAF
  hal_step()
  RUN_BACKGROUND_TASKS
    background_callback_run_all()
      port_background_task()           time-gated: ~1ms
        supervisor_tick()              called directly (no queue)
          displayio_background()
      drain callback queue             (for any registered callbacks)
  hal_export_dirty()

VM backwards branch
  MICROPY_VM_HOOK_LOOP = wasm_vm_hook_loop()
    serial_check_interrupt()           Ctrl-C
    line number extraction             debug trace
    budget check                       yield if >= 13ms

VM function return
  MICROPY_VM_HOOK_RETURN = RUN_BACKGROUND_TASKS
    background_callback_run_all()
      port_background_task()           time-gated (cheap no-op if <1ms)
      drain callback queue
```

## What this preserves

- `background_callback_add/run_all/prevent/allow/reset/gc_collect` — full API
- `supervisor_enable/disable_tick` — refcounted, no-op internally
- `supervisor_ticks_ms64/32/ms` — timing for asyncio
- `supervisor_background_ticks_ok` — watchdog-style check
- `port_wake_main_task` → `sh_set_bg_pending` — JS scheduling signal

## What this removes

- `wasm_background_tasks()` — 5 lines, orphaned indirection
- `tick_callback` static — no longer enqueued
- `port_background_tick()` / `port_start_background_tick()` / `port_finish_background_tick()` calls — stubs called from nowhere
- Queue round-trip in `supervisor_tick()` — direct call instead
