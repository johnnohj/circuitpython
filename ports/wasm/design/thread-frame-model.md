# Thread-as-Frame: VM Integration via Thread Bookends

**Status**: Design (2026-04-25)
**Replaces**: SUSPEND sentinel mechanism (NLR-based)
**Depends on**: Port chassis (MEMFS-in-linear-memory)

## Core idea

Each `chassis_frame()` call is one "thread turn."  The VM runs as a
MicroPython thread whose `mp_state_thread_t` lives in `port_mem`
(MEMFS-backed linear memory).  The frame bookends are thread
init/deinit:

```
chassis_frame():
    mp_thread_set_state(&port_mem.thread_state)   // ENTER
    mp_execute_bytecode(code_state)               // RUN
    return                                        // EXIT — state persists
```

There's only one thread at a time, so `mp_thread_is_main_thread()`
is always true.  The JS rAF loop is the "scheduler" that gives the
VM its time slice.

## Stopping the VM: callback-deferred yield

### The problem

In a `while True:` loop, `mp_execute_bytecode` never returns.  We need
a mechanism to make it stop and return to JS.

### Current mechanism (SUSPEND — to be replaced)

1. HOOK_LOOP checks budget → sets `mp_vm_should_yield` flag
2. `vm.c:1401` returns `MP_VM_RETURN_SUSPEND`
3. But if we're inside a C call chain (`fun_bc_call`), SUSPEND can't
   propagate through C frames as a return code
4. So `fun_bc_call` raises `mp_vm_suspend_sentinel` (SystemExit subclass)
   via NLR to unwind the C stack
5. vm.c exception handler has identity checks to skip traceback and
   try/except for the sentinel
6. objgenerator.c has special SUSPEND handling

This works but touches 4 files in upstream MicroPython code.

### New mechanism: schedule stop at HOOK_LOOP, execute at HOOK_RETURN

The insight: **HOOK_RETURN already drains the background callback queue.**
And HOOK_RETURN fires at every function return — INSIDE
`mp_execute_bytecode`, BEFORE the stackless chain pops the code_state.

```
HOOK_LOOP (backwards branch, budget > soft deadline):
    DON'T yield immediately
    Schedule a callback: set_should_yield

HOOK_RETURN (function return):
    background_callback_run_all()        ← already happens (mpconfigport.h:326)
    → set_should_yield callback fires
    → mp_vm_should_yield = true

    The function return completes NORMALLY.
    fun_bc_call gets MP_VM_RETURN_NORMAL + a proper return value.
    No NLR sentinel needed.

    Control returns to the stackless chain.
    At the NEXT backwards branch:
    → vm.c:1401 checks mp_vm_should_yield → true
    → saves IP/SP to code_state (on pystack, in linear memory)
    → return MP_VM_RETURN_SUSPEND
    → clean return — we're at the stackless level, no C frames to unwind
```

### Two-tier budget with internal sentinel

The soft path (callback) handles most cases.  But a tight loop with
no function calls (`while True: x += 1`) never hits HOOK_RETURN, so
the callback never fires.  The firm deadline catches this with a
direct yield — and if that yield hits a C-call boundary (fun_bc_call),
we raise an internal sentinel.

The sentinel is caught at `chassis_frame` via `nlr_push` — the same
pattern as `thread_entry` in modthread.c.  User code never sees it.

```
 Budget    Mechanism            Path
 ──────    ─────────            ────
 < 8ms     Power on             Keep running
 8ms       Schedule callback    → HOOK_RETURN drains → flag set
 (soft)                         → next backwards branch → return SUSPEND
                                → fun_bc_call returns normally (no exception)
 10ms      Raise sentinel       → NLR unwinds C stack
 (firm)                         → caught at chassis_frame nlr_push
                                → swallowed, treated as SUSPEND
                                → user code never sees it
```

```c
uint32_t chassis_frame(double now_ms, double budget_ms) {
    ...
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        rc = mp_execute_bytecode(code_state);
        nlr_pop();
    } else {
        // Firm-deadline sentinel caught here — swallowed
        rc = MP_VM_RETURN_SUSPEND;
    }
    ...
}
```

### What changes vs current SUSPEND

| Current (4 upstream files) | New (2 upstream files) |
|---|---|
| `mp_vm_suspend_sentinel` (SystemExit subclass) | Simple static object (not SystemExit) |
| vm.c: traceback skip (identity check) | vm.c: traceback skip (same, still needed) |
| vm.c: try/except bypass (identity check) | vm.c: try/except bypass (same, still needed) |
| objfun.c: raise sentinel on SUSPEND | objfun.c: raise sentinel on SUSPEND (same) |
| objgenerator.c: SUSPEND case × 2 | **Removed** — generators see NORMAL return |
| vm.c: YIELD_FROM SUSPEND rewind | **Removed** — not reached via soft path |

The soft path eliminates generator/YIELD_FROM complexity.  The firm
path still needs the vm.c identity checks and fun_bc_call raise, but
fires rarely (only tight loops inside C-called functions).

### What remains (unchanged)

| Kept | File | Why |
|------|------|-----|
| `MP_VM_RETURN_SUSPEND` enum value | runtime.h | Still how mp_execute_bytecode exits |
| `mp_vm_should_yield` check | vm.c:1401 | Budget check at backwards branch |
| `MICROPY_VM_YIELD_SAVE_STATE` | vm.c:1409 | Saves resume code_state |
| `MICROPY_VM_HOOK_RETURN` = `RUN_BACKGROUND_TASKS` | mpconfigport.h | Drains callbacks |
| Sentinel identity checks | vm.c:1518,1561 | Needed for firm deadline path |
| Sentinel raise in fun_bc_call | objfun.c:354 | Needed for firm deadline path |

### The budget model

- **< 8ms**: power on, keep running
- **8ms – 10ms**: wrapping up — callback fires at next function return,
  yield flag set, clean return at next backwards branch.  This is the
  common path.  No exceptions, no NLR.
- **10ms** (firm): hard stop — sentinel raised, NLR unwinds to
  chassis_frame.  Only fires for tight loops with no function calls
  inside C-called functions.  Rare in practice.

## Thread state as frame bookend

### mp_state_thread_t in port_mem

The thread state contains everything the VM needs to resume:

```c
// In port_memory_t:
mp_state_thread_t thread_state;  // MEMFS: /py/thread
```

Fields that matter:
- `pystack_start/end/cur` → point to `/py/ctx/0/pystack`
- `nlr_top` → exception handler chain (NULL between frames)
- `dict_locals/dict_globals` → scope pointers (in GC heap)
- `gc_lock_depth` → GC lock state
- `mp_pending_exception` → Ctrl-C, etc.

### Frame lifecycle

```c
uint32_t chassis_frame(double now_ms, double budget_ms) {
    budget_frame_start(budget_ms);
    drain_events();
    hal_step();

    if (!vm_has_work()) return RC_DONE;

    // ── ENTER: install thread state ──
    mp_thread_set_state(&port_mem.thread_state);
    // pystack, NLR, globals now active
    // mp_thread_is_main_thread() → true

    // ── RUN: VM burst ──
    mp_vm_return_kind_t rc = mp_execute_bytecode(
        port_mem.resume_code_state);

    // rc = SUSPEND → budget expired, more work
    // rc = NORMAL  → code finished
    // rc = EXCEPTION → error

    // ── EXIT: nothing to do ──
    // thread_state is in port_mem → persists in linear memory
    // code_state is on pystack → persists in linear memory
    // C stack frames evaporate — that's fine

    if (rc == MP_VM_RETURN_SUSPEND) {
        ffi_request_frame();
        return RC_YIELD;
    }
    return RC_DONE;
}
```

### mp_thread_set_state is the key

Currently a no-op on WASM.  In this model, it becomes real:

```c
// mpthreadport.c — WASM implementation
static mp_state_thread_t *current_thread_state;

mp_state_thread_t *mp_thread_get_state(void) {
    return current_thread_state;
}

void mp_thread_set_state(mp_state_thread_t *state) {
    current_thread_state = state;
}
```

With `MICROPY_PY_THREAD` enabled, `MP_STATE_THREAD(x)` calls
`mp_thread_get_state()->x` instead of accessing `mp_state_ctx.thread.x`
directly.  This lets us swap thread states between frames (or
between contexts, for future multi-context support).

### mp_thread_is_main_thread

```c
#define mp_thread_is_main_thread() \
    (mp_thread_get_state() == &mp_state_ctx.thread)
```

If we set `current_thread_state = &mp_state_ctx.thread`, this is true.
If we later support multiple contexts, each context gets its own
`mp_state_thread_t`, and only context 0 is "main."

## HOOK_LOOP implementation

### Mechanism: `mp_sched_vm_abort` (MICROPY_ENABLE_VM_ABORT)

MicroPython has a built-in mechanism for exactly this: `vm_abort`.
It's designed for asynchronous abort from outside the VM, jumping
directly to a pre-set catch point — bypassing ALL exception handling,
try/except, generators, fun_bc_call.  No exception object, no NLR
sentinel, no identity checks.

```c
// At init (chassis_frame entry):
nlr_buf_t abort_buf;
nlr_set_abort(&abort_buf);  // set catch point

// From HOOK_LOOP (when budget expires):
mp_sched_vm_abort();        // set flag — checked at vm.c:1435

// At backwards branch (vm.c:1437-1439):
mp_handle_pending(true);    // checks vm_abort → nlr_jump_abort()

// nlr_jump_abort bypasses ALL nlr_push points (try/except, fun_bc_call)
// and lands directly at chassis_frame's abort_buf.
```

### Two-tier budget

```c
// mpconfigport.h
extern void wasm_vm_hook_loop(const void *code_state_ptr);
#define MICROPY_VM_HOOK_LOOP  wasm_vm_hook_loop(code_state);
#define MICROPY_VM_HOOK_RETURN  background_callback_run_all();
```

```c
// supervisor/vm_yield.c (simplified)

static background_callback_t yield_callback;

static void set_yield_flag(void *unused) {
    (void)unused;
    mp_vm_request_yield();  // sets the flag checked at vm.c:1401
}

void wasm_vm_hook_loop(const void *code_state_ptr) {
    if (serial_check_interrupt()) {
        mp_sched_keyboard_interrupt();
    }

    supervisor_tick();

    if (budget_soft_expired()) {
        // Tier 1 (soft, 8ms): schedule callback for next HOOK_RETURN.
        // If a function returns before the firm deadline, the callback
        // fires → yield flag set → clean return at next backwards branch.
        background_callback_add(&yield_callback, set_yield_flag, NULL);

        // Also set the yield flag directly.  At the NEXT backwards branch,
        // vm.c:1401 saves IP/SP and returns MP_VM_RETURN_SUSPEND.
        // If we're in the stackless chain (common case), this propagates
        // cleanly to chassis_frame.  If we're inside fun_bc_call (rare),
        // fun_bc_call sees SUSPEND — but that's handled by Tier 2.
        mp_vm_request_yield();
    }

    if (budget_firm_expired()) {
        // Tier 2 (firm, 10ms): abort.  nlr_jump_abort() jumps directly
        // to chassis_frame's abort point, bypassing ALL Python exception
        // handling.  No sentinel, no identity checks, no try/except bypass.
        // State was already saved by vm.c:1401 at the soft deadline.
        mp_sched_vm_abort();
    }
}
```

### Flow at backwards branch (vm.c)

```
vm.c line 1387:  MICROPY_VM_HOOK_LOOP
                   → wasm_vm_hook_loop runs
                   → if budget > 8ms: set yield flag + schedule callback
                   → if budget > 10ms: mp_sched_vm_abort()

vm.c line 1401:  if (mp_vm_should_yield())
                   → saves ip, sp, exc_sp to code_state    ← STATE SAVED
                   → MICROPY_VM_YIELD_SAVE_STATE            ← resume point saved
                   → return MP_VM_RETURN_SUSPEND
                   → if we're in stackless chain: clean return to chassis_frame
                   → if we're in fun_bc_call: SUSPEND returned to caller...
                     (but vm_abort fires at next branch — see below)

vm.c line 1435:  || MP_STATE_VM(vm_abort)
                   → mp_handle_pending(true)
                   → nlr_jump_abort()
                   → jumps directly to chassis_frame's nlr_set_abort point
                   → state was ALREADY saved by vm.c:1401 above
```

### Why this works

The vm.c backwards-branch sequence is:
1. HOOK_LOOP (line 1387) — our code runs, sets flags
2. Yield check (line 1401) — **saves state**, returns SUSPEND
3. Pending check (line 1420-1439) — checks vm_abort, fires abort

If `should_yield` is set, step 2 saves state and returns SUSPEND.
If SUSPEND can't propagate (C call boundary), the next backwards
branch hits step 3 with `vm_abort` set → `nlr_jump_abort()` to
chassis_frame.  State is already saved from step 2.

If the loop is so tight that we never hit step 2 (budget jumps from
<8ms to >10ms in one iteration — extremely unlikely), step 3 fires
without saved state.  This is acceptable: we're aborting, not
suspending.  The code will restart from the last saved point.

### What this uses (all upstream, no changes)

| Feature | API | File |
|---------|-----|------|
| Abort flag | `mp_sched_vm_abort()` | py/scheduler.c |
| Abort catch point | `nlr_set_abort(&buf)` | py/nlr.h |
| Abort jump | `nlr_jump_abort()` | py/nlr.c |
| Abort check | `vm_abort` flag in vm.c | py/vm.c:1435 |
| Yield flag | `mp_vm_request_yield()` | (existing port code) |
| Yield save | `vm.c:1401-1412` | py/vm.c |
| Callbacks | `background_callback_add/run_all` | (existing port code) |

### What this eliminates (vs current SUSPEND)

| Removed | Why |
|---------|-----|
| `mp_vm_suspend_sentinel` (SystemExit subclass) | vm_abort has no exception object |
| vm.c traceback identity check | vm_abort bypasses exception handler entirely |
| vm.c try/except identity check | vm_abort bypasses exception handler entirely |
| fun_bc_call sentinel raise | vm_abort jumps over fun_bc_call |
| objgenerator.c SUSPEND cases | vm_abort jumps over generators |
| vm.c YIELD_FROM SUSPEND rewind | vm_abort jumps over YIELD_FROM |

**Zero upstream code changes.  All mechanisms already exist.**

## Relationship to GIL

The GIL bounce in upstream MicroPython (`vm.c:1456-1457`):
```c
MP_THREAD_GIL_EXIT();   // release — other threads can run
MP_THREAD_GIL_ENTER();  // re-acquire — blocks until available
```

In our model:
```
chassis_frame returns    → GIL_EXIT  (JS gets its turn)
JS runs rAF, UI, events
chassis_frame called     → GIL_ENTER (VM gets its turn)
```

We don't need the actual GIL mutex.  The frame boundary IS the GIL
bounce.  JS and C are cooperative — they never run simultaneously
(single-threaded WASM).  The budget is the "quantum."

But we DO need the vm.c:1401 check (or equivalent) to make
`mp_execute_bytecode` actually return.  The GIL bounce doesn't help
because on a single-threaded port, `GIL_EXIT(); GIL_ENTER();` is
a no-op.  The callback-deferred yield is our mechanism.

## What about mp_thread_finish?

`mp_thread_finish()` on unix removes the thread from the thread list
and is called when a thread's entry function completes.  It's a
lifecycle signal, not a "stop executing" signal.

For our model:
- `mp_thread_finish()` would be called when code.py or REPL completes
  (not on every frame yield)
- Frame yields don't "finish" the thread — they pause it
- `mp_thread_start()` would be called at first frame after mp_init

The per-frame yield is handled entirely by the callback mechanism +
`MP_VM_RETURN_SUSPEND`.  Thread start/finish are lifecycle bookends
around the entire code.py/REPL session, not per-frame.

## Migration path

### Step 1: Enable MICROPY_PY_THREAD on WASM

Make `mp_thread_set_state` real (not a no-op).  Store a
`mp_state_thread_t *` that `mp_thread_get_state` returns.
Thread state lives in `port_mem`.

### Step 2: Two-tier budget with callback + vm_abort

1. Enable `MICROPY_ENABLE_VM_ABORT` in mpconfigport.h
2. Change `wasm_vm_hook_loop`:
   - Soft (8ms): `mp_vm_request_yield()` + schedule callback
   - Firm (10ms): `mp_sched_vm_abort()`
3. `chassis_frame` sets `nlr_set_abort(&abort_buf)` at entry
4. **Remove** `mp_vm_suspend_sentinel` and all identity checks
5. **Remove** fun_bc_call SUSPEND → nlr_raise path
6. **Remove** objgenerator.c SUSPEND handling
7. **Remove** vm.c YIELD_FROM SUSPEND rewind
8. Keep vm.c:1401 yield check (saves state) — this is upstream code

### Step 3: Thread bookends in chassis_frame

1. `chassis_frame` calls `mp_thread_set_state` at start
2. Calls `mp_execute_bytecode`
3. Returns to JS — thread_state persists in port_mem

### Step 4: Lifecycle signals

- `mp_thread_start()` at first frame after init
- `mp_thread_finish()` when code.py completes or Ctrl-D
- Soft reboot = `mp_thread_finish()` + `mp_thread_start()`

## Summary

| Concept | Current | New |
|---------|---------|-----|
| Stop VM (common) | NLR sentinel always | yield flag → save state → return SUSPEND |
| Stop VM (C boundary) | NLR sentinel through fun_bc_call | `vm_abort` → jumps to chassis_frame |
| Sentinel object | SystemExit subclass + identity checks | None — vm_abort has no exception object |
| Thread state | mp_state_ctx.thread (fixed) | port_mem.thread_state (MEMFS, swappable) |
| Frame bookend | vm_yield_start/step | mp_thread_set_state + mp_execute_bytecode |
| GIL | No-op on WASM | Frame boundary IS the GIL bounce |
| Budget model | Immediate yield, one threshold | Soft 8ms (yield) + firm 10ms (abort) |
| fun_bc_call changes | Raise sentinel on SUSPEND | **Removed** — vm_abort jumps over it |
| objgenerator.c changes | 2 SUSPEND cases | **Removed** — vm_abort jumps over it |
| vm.c exception handler | 2 identity checks | **Removed** — vm_abort bypasses handler |
| vm.c YIELD_FROM rewind | Complex ip/sp restore | **Removed** — vm_abort bypasses it |
| Upstream code changes | 4 files modified | **0 files** — uses existing vm_abort |
| User-visible effects | None | None — abort is invisible |
