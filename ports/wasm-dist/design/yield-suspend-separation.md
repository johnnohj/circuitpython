# Yield / Suspend Separation — Design & Implementation Plan

**Status**: Planned, not yet implemented
**Date**: 2026-04-16
**Gate**: `MICROPY_VM_YIELD_ENABLED` (existing macro)

---

## Problem

`MP_VM_RETURN_YIELD` is overloaded. It means both:

1. **Python-level yield** — a generator hit a `yield` expression. The top of stack
   holds the yielded value. Generators and `async def` / `await` (which compiles to
   generator `yield`) depend on this meaning.
2. **Supervisor suspend** — `mp_vm_should_yield()` returned true at a backwards
   branch. The VM saved ip/sp/exc_sp_idx via `MICROPY_VM_YIELD_SAVE_STATE` and
   returned `MP_VM_RETURN_YIELD` as a signal to the outer driver.

Both paths return the same enum value. When the two meet — a budget suspend fires
inside a generator's bytecode — the caller (`mp_obj_gen_resume` in
`py/objgenerator.c`) cannot distinguish them. It assumes "generator yielded",
reads `*self->code_state.sp` as the yielded value (actually garbage from the
suspend save point), and reports normal yield semantics to its caller (typically
asyncio's event loop).

Downstream effects observed (2026-04-16, Phase 1 Python supervisor attempt):

- `asyncio.run(coro)` exits after one pass through `await asyncio.sleep(0)`
- `import asyncio` from a frozen main.py triggers module unregistration
- Any code path where suspend fires mid-generator silently corrupts scheduling

## Approach

Add a fourth return code: **`MP_VM_RETURN_SUSPEND`**. Semantics:

- Emitted by `mp_vm_should_yield()` path at backwards branches in `py/vm.c`.
- Propagated unchanged through every `mp_execute_bytecode` caller that cares
  about distinguishing Python yields from supervisor suspends — primarily
  `mp_obj_gen_resume`, `mp_resume`, and the `MP_BC_YIELD_FROM` handler.
- Consumed by `vm_yield_step` (same handling as `MP_VM_RETURN_YIELD` today:
  context is yielded, next frame will resume via saved code_state).

`MP_VM_RETURN_YIELD` keeps its original, narrow meaning: Python `yield` expression.

### Why "SUSPEND"?

Terminology comparison:

| Candidate | Verdict |
|---|---|
| `RETURN_YIELD_BUDGET` | Awkward; conflates with Python yield |
| `RETURN_AWAIT` | Misleading — `await` compiles to `yield` at bytecode level |
| `RETURN_TIMEOUT` | Implies failure/expiration; budget exhaustion isn't a timeout |
| `RETURN_PROMISE` | JavaScript concept, alien to MicroPython |
| `RETURN_PAUSE` | Colloquial; imprecise |
| **`RETURN_SUSPEND`** | **Chosen.** Standard scheduler terminology (Go/Erlang/Rust all use "suspend" for coroutine/task pause). Future-proof across suspend reasons (budget, sleep, display, I/O). |

Reason-for-suspend (budget / sleep / show / io_wait / stdin) already lives in
`_yield_reason` in `supervisor/vm_yield.c` and is orthogonal to the return code.

### Upstream-friendliness

All changes to `py/` files are gated by `#if MICROPY_VM_YIELD_ENABLED`.
Upstream behavior is unchanged when the macro is 0 (its default).
This keeps the patches minimal and rebase-friendly.

---

## Phases

Each phase is a single commit. Rollback is clean between phases. Run the full
test suite (`node test_standard.mjs && node test_browser.mjs`, 55 + 55 tests)
after every phase.

### Phase 1 — Enum value

**Goal**: reserve the identifier; no behavior change.

**Files**:
- `py/runtime.h` (lines 46–50) — add `MP_VM_RETURN_SUSPEND` under
  `#if MICROPY_VM_YIELD_ENABLED`.

```c
typedef enum {
    MP_VM_RETURN_NORMAL,
    MP_VM_RETURN_YIELD,
    MP_VM_RETURN_EXCEPTION,
    #if MICROPY_VM_YIELD_ENABLED
    MP_VM_RETURN_SUSPEND,
    #endif
} mp_vm_return_kind_t;
```

**Verification**:
- Both variants compile. Full test suite green.
- No behavioral change.

**Risk**: zero.

---

### Phase 2 — VM emits SUSPEND at backwards-branch check

**Goal**: the supervisor budget-exhaustion path returns SUSPEND; Python yield
path unchanged.

**Files**:
- `py/vm.c:~1372` — inside `#if MICROPY_VM_YIELD_ENABLED`:
  change `return MP_VM_RETURN_YIELD;` → `return MP_VM_RETURN_SUSPEND;`
  (only in the `mp_vm_should_yield()` branch; lines 1217–1223 stay YIELD).
- `supervisor/vm_yield.c:~250` (vm_yield_step switch) — add case:
  ```c
  case MP_VM_RETURN_SUSPEND:
      return 1;  /* same as YIELD: code is paused, resume next frame */
  ```

**Verification**:
- `while True: time.sleep(0)` in code.py yields each frame (path: vm.c → SUSPEND
  → vm_yield_step → 1).
- Generator/yield-from code unchanged (still returns YIELD).
- All 110 tests green.

**Risk**: low. Single return statement change, single switch-case addition.

---

### Phase 3 — Generator-resume boundary propagation

Three sub-steps. Test between each.

#### 3a — `mp_obj_gen_resume` passes SUSPEND through

**File**: `py/objgenerator.c:~280` (switch on ret_kind in mp_obj_gen_resume).

```c
switch (ret_kind) {
    case MP_VM_RETURN_NORMAL: /* unchanged */ break;
    case MP_VM_RETURN_YIELD:  /* unchanged — real generator yield */ break;
    case MP_VM_RETURN_EXCEPTION: /* unchanged */ break;
    #if MICROPY_VM_YIELD_ENABLED
    case MP_VM_RETURN_SUSPEND:
        /* Supervisor suspended the generator's bytecode frame mid-execution.
         * code_state is preserved (saved by MICROPY_VM_YIELD_SAVE_STATE in vm.c).
         * DO NOT read code_state->sp as a yielded value — the stack pointer
         * is at a backwards branch, not a yield opcode.  Generator is still
         * live; next gen_resume will continue from saved ip. */
        break;
    #endif
}
return ret_kind;
```

**Verification**: new test — `asyncio.run` of a coroutine with
`while True: await asyncio.sleep(0)` runs N frames without exiting prematurely.

#### 3b — `mp_resume` wrapper propagates SUSPEND

**File**: `py/runtime.c` (mp_resume, lines ~1495–1590).

Audit all sites returning `MP_VM_RETURN_YIELD` in this function:
- `1529` (iternext success) — Python-level yield; unchanged
- `1548` (__next__ method) — Python-level yield; unchanged
- `1558` (send method) — Python-level yield; unchanged
- `1579` (throw method) — Python-level yield; unchanged

These are wrappers that call Python-level methods and translate their return
into a yield. They don't call `mp_execute_bytecode` directly, so SUSPEND
can't originate inside them.

The sole path that matters: `mp_resume` → `mp_obj_gen_resume` (line 1521).
After Phase 3a, `mp_obj_gen_resume` returns SUSPEND directly. `mp_resume`
returns it by value from line 1521 (`return mp_obj_gen_resume(...)`). No
additional change needed, **but add a comment documenting why SUSPEND can
flow through here**.

**Verification**: `yield from` chains where the inner generator suspends mid-frame.

#### 3c — `MP_BC_YIELD_FROM` opcode handles SUSPEND

**File**: `py/vm.c:~1240` (MP_BC_YIELD_FROM handler).

Currently:
```c
if (ret_kind == MP_VM_RETURN_YIELD) {
    ip--;
    PUSH(ret_value);
    goto yield;  /* label at line 1217 — saves state, returns YIELD */
}
```

Add:
```c
#if MICROPY_VM_YIELD_ENABLED
else if (ret_kind == MP_VM_RETURN_SUSPEND) {
    /* Inner generator was suspended by supervisor.  Save our own frame
     * state (like the backwards-branch path) and propagate SUSPEND. */
    nlr_pop();
    code_state->ip = ip;    /* re-run YIELD_FROM on resume (ip-- first? see yield label) */
    code_state->sp = sp;
    code_state->exc_sp_idx = MP_CODE_STATE_EXC_SP_IDX_FROM_PTR(exc_stack, exc_sp);
    #ifdef MICROPY_VM_YIELD_SAVE_STATE
    MICROPY_VM_YIELD_SAVE_STATE(code_state);
    #endif
    FRAME_LEAVE();
    return MP_VM_RETURN_SUSPEND;
}
#endif
```

**Note on IP semantics**: the `yield` label at line 1217 is reached by the
YIELD_VALUE opcode, whose ip has already been advanced past the opcode.
YIELD_FROM is different — if the inner generator yielded a value, the code
at line 1241 does `ip--; PUSH(ret_value); goto yield;` so the YIELD_FROM
opcode is re-executed on resume. For SUSPEND we should do the same thing:
re-execute YIELD_FROM when resumed, so the inner generator is re-sent
into. Verify `ip--` is needed here as well; test with a nested await.

**Verification**: nested `async def` with multiple `await` levels, each
suspending mid-frame. Resume correctly continues the nested coroutine.

---

### Phase 4 — `fun_bc_call` handles SUSPEND

**Goal**: C-calls-Python paths (imports, `mp_call_function_*`) don't crash on
SUSPEND return code.

**Current state** (`py/objfun.c:337–346`):

```c
} else if (vm_return_kind == MP_VM_RETURN_YIELD) {
    extern mp_obj_exception_t mp_vm_yield_exception;
    nlr_raise(MP_OBJ_FROM_PTR(&mp_vm_yield_exception));
}
```

After Phase 2, `mp_execute_bytecode` returns SUSPEND here (was YIELD). The
current if-chain doesn't match, falling through to
`assert(vm_return_kind == MP_VM_RETURN_EXCEPTION)` — a crash.

**Fix**:

```c
#if MICROPY_VM_YIELD_ENABLED
} else if (vm_return_kind == MP_VM_RETURN_SUSPEND) {
    /* Supervisor suspended a C-called Python frame (e.g. during import,
     * or via explicit mp_call_function_*).  State is saved in
     * mp_vm_yield_state.  Propagate via nlr_raise to unwind the C stack;
     * vm_yield_step catches and returns "yielded".
     *
     * The exception type is a dedicated suspend sentinel (not a
     * Python-visible class), so `except BaseException` won't catch it. */
    extern mp_obj_exception_t mp_vm_suspend_sentinel;
    nlr_raise(MP_OBJ_FROM_PTR(&mp_vm_suspend_sentinel));
#endif
```

**Rename** `mp_vm_yield_exception` → `mp_vm_suspend_sentinel` in
`supervisor/vm_yield.c`. Consider changing its type from `SystemExit`
subclass to a dedicated, non-Python-catchable type. Options:

- **4a**: keep as SystemExit subclass; rely on Phase 3 having removed all
  the paths where it would get caught at Python level. This is the
  minimal change.
- **4b**: make it a standalone base object (not a BaseException subclass).
  Python code cannot catch it. But `mp_obj_print_exception` and similar
  may need to handle it gracefully. More invasive.

**Recommendation**: 4a first. If we hit a case where Python code catches
the sentinel, escalate to 4b.

**Files**:
- `py/objfun.c:~337` — add SUSPEND branch (gated).
- `supervisor/vm_yield.c:47–51` — rename identifier; keep type.
- `supervisor/vm_yield.c:~263–283` — update exception-matching code to
  use new name; `mp_obj_is_subclass_fast(exc, SystemExit)` check still
  catches it (option 4a).

**Critical: `vm_yield_step` MP_VM_RETURN_EXCEPTION handler must
distinguish the yield sentinel from genuine SystemExit**

Current behavior (`supervisor/vm_yield.c:271-282`): any SystemExit
subclass returns 0 (soft reboot / done), tearing down vm->code_state.
When Phase 4 lands NLR-based propagation, the outer mp_execute_bytecode
will return EXCEPTION with `mp_vm_yield_exception` in state[0], and
this handler would incorrectly terminate execution instead of
recognizing it as suspension.

Required change (part of Phase 4):

```c
case MP_VM_RETURN_EXCEPTION: {
    mp_obj_t exc = MP_OBJ_FROM_PTR(vm->code_state->state[0]);
    #if MICROPY_VM_YIELD_ENABLED
    // Distinguish yield sentinel from genuine SystemExit/exit().
    // Sentinel is an identity check — we raised this exact static object.
    if (exc == MP_OBJ_FROM_PTR(&mp_vm_suspend_sentinel)) {
        // State already saved in mp_vm_yield_state by vm.c before the
        // nlr_raise.  Next cp_step resumes at innermost suspended frame.
        return 1;
    }
    #endif
    if (mp_obj_is_subclass_fast(...SystemExit...)) {
        /* genuine exit — unchanged */
    }
    ...
}
```

Identity check (`exc == &mp_vm_suspend_sentinel`) is safer than
subclass check — the sentinel is a static singleton, never constructed
at Python level, so pointer equality is sufficient and cannot be
confused with a user-raised SystemExit instance.

**Verification**:
- Frozen asyncio import with yield during init works.
- `import some_module` where module code has a long loop works across
  frame boundaries.
- No Python-level `except BaseException` catches the sentinel (verify
  by writing a test that tries).

**Risk**: medium. The NLR raise path is delicate. Test import scenarios
thoroughly.

---

### Phase 5 — Cleanup & documentation

**Goals**:
- Rename identifiers consistently
- Remove dead or confusing comments about YIELD-meaning-both-things
- Update memory files

**Files**:
- `supervisor/vm_yield.c` — update header comments to reflect
  yield-vs-suspend distinction
- `py/objfun.c` — cleaned-up comments in the return-kind block
- Memory:
  - Close out `supervisor_scheduler_asyncio.md` or update it to note
    the mechanism fix
  - New memory: `vm_suspend_return_code.md` documenting the enum and
    propagation model
- `design/yield-suspend-separation.md` (this file) — mark as
  "Implemented, see commits X/Y/Z"

**Risk**: zero.

---

## Cross-cutting concerns

### `mp_vm_yield_state` per-context

The single global `mp_vm_yield_state` points at the innermost code_state
that was suspended. **This must be per-context**, not truly global —
otherwise a suspend on context 1 would overwrite context 0's saved state.

The current code already handles this: `supervisor/context.c` / `vm_yield.c`
store per-context VM state via `cp_context_vm_t`, and context switching
swaps the relevant pointers. **Verify during Phase 2** that the
SUSPEND return path correctly uses per-context state.

### Native code emission

Browser variant has `MICROPY_EMIT_WASM` and `MICROPY_EMIT_NATIVE`
disabled (confirmed in mpconfigboard.h). We can skip `py/asmwasm.c`
and `py/emitnative.c` SUSPEND support for now. If either is ever
enabled, those files reference `MP_VM_RETURN_YIELD` hardcoded at
`asmwasm.c:487–503` and `emitnative.c:3134` — they'd need the same
treatment.

### Frame budget crossing context boundaries

When the supervisor suspends a context and schedules a different one,
the second context's bytecode runs in the same `cp_step` frame.
`_frame_start_ms` is the frame-global deadline; every context shares
that. The existing code in `vm_yield.c:~135` already checks this:

```c
if (!wasm_cli_mode && !_yield_requested && !mp_thread_in_atomic_section()) {
    uint64_t now = _wall_clock_ms();
    if (now - _frame_start_ms >= _frame_budget_ms) {
        ...
    }
}
```

The `_frame_start_ms` is frame-local; the budget is shared. Context
switches happen in `cp_step` (not inside `mp_vm_should_yield`), so
this is correct. Each context gets to try to make progress within the
remaining frame budget.

---

## Testing strategy

Each phase adds to the test suite. Target coverage:

| Scenario | Phase | Test file |
|---|---|---|
| Existing tests still pass | All | `test_standard.mjs`, `test_browser.mjs` |
| `while True: time.sleep(0)` in code.py yields per frame | 2 | new: `test_browser.mjs` |
| asyncio task yields via await sleep(0) without exit | 3a | new |
| asyncio task with yield-from chain suspends cleanly | 3b/3c | new |
| Frozen module import with internal loop yields, resumes | 4 | new |
| Python-level `except BaseException` does NOT catch suspend | 4 | new |

Initial-state tests (pre-Phase-1) should also be captured as
regression baselines:

- `test_browser.mjs: asyncio.sleep(0) loop — currently exits
  prematurely after 1 frame`. This test will _fail_ until Phase 3a
  lands; commit it at Phase 3a along with the fix.

---

## Rollback strategy

Each phase is independent:

- Phase 1 revertible alone (remove enum value)
- Phase 2 revertible alone (change back to YIELD)
- Phase 3a/b/c each revertible independently
- Phase 4 revertible alone (fall back to current YIELD-then-ifchain-miss
  behavior — which is broken today, so realistically Phase 4 can't be
  reverted without also reverting Phase 2)

If Phase 3 regresses a generator/yield-from test we didn't anticipate,
isolate by reverting the most recent sub-step.

---

## References

- `MEMORY/supervisor_scheduler_asyncio.md` — why this matters
- `py/runtime.h` — enum definition site
- `py/vm.c:1217-1373` — yield/suspend emission sites
- `py/objgenerator.c:207-317` — generator resume boundary
- `py/runtime.c:1495-1590` — mp_resume wrapper
- `py/objfun.c:258-370` — fun_bc_call and NLR yield mechanism
- `supervisor/vm_yield.c` — supervisor side of the protocol
- Revert commit: `eab2661be2` (Phase 1 Python supervisor; this plan
  makes Phase 1 feasible)
