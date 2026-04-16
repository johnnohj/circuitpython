/*
 * supervisor/vm_yield.c — Wall-clock frame budget + VM yield protocol.
 *
 * The JS host calls cp_step() once per requestAnimationFrame (~60fps).
 * The supervisor sets _frame_start_ms and enters the VM.  At every
 * backwards branch, MICROPY_VM_HOOK_LOOP calls wasm_vm_hook_loop()
 * which:
 *   1. Runs port background tasks (Ctrl-C check, hw endpoint polling)
 *   2. Checks the wall-clock budget
 *   3. If budget expired, calls mp_vm_request_yield(YIELD_BUDGET)
 *
 * Immediately after the hook, the MICROPY_VM_YIELD_ENABLED block in
 * py/vm.c checks mp_vm_should_yield().  If the budget set it, the VM
 * saves state (ip, sp, exc_sp, innermost code_state) and returns
 * MP_VM_RETURN_YIELD.  The C stack fully unwinds.  Next cp_step()
 * resumes at the saved code_state.
 *
 * Adapted from ports/wasm-wasi/vm_yield.c — step-counter budget
 * replaced with wall-clock budget.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <time.h>

#include "py/bc.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/objexcept.h"
#include "extmod/vfs.h"

#include "mpthreadport.h"
#include "supervisor/context.h"

/* ------------------------------------------------------------------ */
/* Suspend sentinel                                                    */
/*                                                                     */
/* When a C-called Python frame (fun_bc_call, gen_resume_and_raise)    */
/* observes MP_VM_RETURN_SUSPEND (or the legacy YIELD in the supervisor */
/* yield context), it nlr_raises this static sentinel to unwind the    */
/* C stack.  vm_yield_step identity-checks the sentinel in its         */
/* EXCEPTION handler and treats it as suspension (return 1) rather     */
/* than a genuine SystemExit (return 0 → soft reboot).                 */
/*                                                                     */
/* Typed as SystemExit subclass for NLR compat, but matched by pointer */
/* identity (not subclass check) in the supervisor.  User Python code  */
/* that catches BaseException will also catch this — a known hazard    */
/* for now; future work: make it a non-BaseException type.             */
/* ------------------------------------------------------------------ */

mp_obj_exception_t mp_vm_suspend_sentinel = {
    .base = { &mp_type_SystemExit },  /* SystemExit type for NLR compat */
    .args = (mp_obj_tuple_t *)&mp_const_empty_tuple_obj,
    .traceback = NULL,
};

/* ------------------------------------------------------------------ */
/* Yield reasons                                                       */
/* ------------------------------------------------------------------ */

enum {
    YIELD_BUDGET  = 0,  /* wall-clock budget exhausted (normal yield) */
    YIELD_SLEEP   = 1,  /* time.sleep() — arg is duration in ms */
    YIELD_SHOW    = 2,  /* display refresh requested */
    YIELD_IO_WAIT = 3,  /* waiting for hardware I/O */
    YIELD_STDIN   = 4,  /* waiting for keyboard input */
};

volatile int mp_vm_yield_reason = YIELD_BUDGET;
volatile uint32_t mp_vm_yield_arg = 0;

/* Written by the VM yield handler (MICROPY_VM_YIELD_SAVE_STATE). */
void *mp_vm_yield_state = NULL;

/* ------------------------------------------------------------------ */
/* Wall-clock budget                                                   */
/* ------------------------------------------------------------------ */

/* Set by wasm_supervisor.c before entering the VM. */
static uint64_t _frame_start_ms = 0;
static uint32_t _frame_budget_ms = 13;

/* Yield flag — set by budget check or explicit request. */
static volatile bool _yield_requested = false;

/* Real wall clock for budget checks — clock_gettime advances during
 * execution, unlike wasm_js_now_ms which is set once per frame. */
static uint64_t _wall_clock_ms(void) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

void vm_yield_set_frame_start(uint64_t ms) {
    _frame_start_ms = _wall_clock_ms();
    (void)ms;
    _yield_requested = false;
    mp_vm_yield_reason = YIELD_BUDGET;
}

void vm_yield_set_budget(uint32_t ms) {
    _frame_budget_ms = ms;
}

int mp_vm_should_yield(void) {
    return _yield_requested ? 1 : 0;
}

/* Request immediate yield with a specific reason. */
void mp_vm_request_yield(int reason, uint32_t arg) {
    mp_vm_yield_reason = reason;
    mp_vm_yield_arg = arg;
    _yield_requested = true;
}

/* ------------------------------------------------------------------ */
/* VM hook — runs at every backwards branch                            */
/*                                                                     */
/* This is MICROPY_VM_HOOK_LOOP.  It fires BEFORE the yield check, so */
/* background tasks are guaranteed to have run right before any yield. */
/* ------------------------------------------------------------------ */

/* Forward declaration — implemented in wasm_supervisor.c */
extern void wasm_background_tasks(void);

/* CLI mode flag — set by main() in supervisor.c */
extern bool wasm_cli_mode;

/* JS time source — written by cp_step() each frame. */
extern volatile uint64_t wasm_js_now_ms;

void wasm_vm_hook_loop(void) {
    /* 1. Run port background tasks (Ctrl-C check) */
    wasm_background_tasks();

    /* 2. Check wall-clock budget (skip in CLI mode and atomic sections).
     *    CLI mode uses blocking I/O — no frame budget, no yield.
     *    Browser mode yields when the frame budget is exhausted. */
    if (!wasm_cli_mode && !_yield_requested && !mp_thread_in_atomic_section()) {
        uint64_t now = _wall_clock_ms();
        if (now - _frame_start_ms >= _frame_budget_ms) {
            mp_vm_request_yield(YIELD_BUDGET, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Delay-as-yield                                                      */
/*                                                                     */
/* time.sleep() doesn't block — it sets a target time and yields.      */
/* The deadline is stored per-context so sleeping contexts don't block  */
/* other contexts from running.                                        */
/* ------------------------------------------------------------------ */

int mp_hal_delay_active(void) {
    int id = cp_context_active();
    uint64_t du = cp_context_get_delay(id);
    if (du == 0) {
        return 0;
    }
    if ((uint64_t)mp_hal_ticks_ms() >= du) {
        cp_context_set_delay(id, 0);
        return 0;
    }
    return 1;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    if (ms == 0) {
        return;
    }
    if (wasm_cli_mode) {
        /* CLI mode: busy-wait (no scheduler to resume a yielded context). */
        uint64_t start = (uint64_t)mp_hal_ticks_ms();
        while ((uint64_t)mp_hal_ticks_ms() - start < ms) {
            mp_event_wait_ms(1);
        }
        return;
    }
    uint64_t deadline = (uint64_t)mp_hal_ticks_ms() + ms;
    int id = cp_context_active();
    cp_context_set_delay(id, deadline);
    cp_context_set_sleeping(id, deadline);
    mp_vm_request_yield(YIELD_SLEEP, (uint32_t)ms);
}

/* ------------------------------------------------------------------ */
/* Stepping state — per-context via cp_context_vm_t                    */
/*                                                                     */
/* All stepping state is indexed by the active context id.  Context    */
/* switching (cp_context_save/restore) swaps pystack pointers and      */
/* yield_state; the _vm[] array in context.c holds per-context         */
/* code_state, started, and first_entry flags.                         */
/* ------------------------------------------------------------------ */

bool vm_yield_code_running(void) {
    cp_context_vm_t *vm = cp_context_vm(cp_context_active());
    return vm->started;
}

/*
 * Start stepping a pre-compiled code_state (from cp_compile_str/file).
 * Writes to the active context's VM state.
 */
void vm_yield_start(mp_code_state_t *cs) {
    cp_context_vm_t *vm = cp_context_vm(cp_context_active());
    vm->code_state = cs;
    vm->started = true;
    vm->first_entry = true;
    cp_context_set_delay(cp_context_active(), 0);
    mp_vm_yield_state = NULL;
}

/*
 * Execute one burst of bytecodes for the active context.
 *
 * Returns:
 *   0 — normal completion (code finished)
 *   1 — yielded (call again next frame)
 *   2 — exception (traceback printed to stderr)
 */
int vm_yield_step(void) {
    cp_context_vm_t *vm = cp_context_vm(cp_context_active());

    if (!vm->started || vm->code_state == NULL) {
        return 0;
    }

    /* If sleeping, don't enter the VM */
    if (mp_hal_delay_active()) {
        return 1;
    }

    /* Reset yield flag for this burst */
    _yield_requested = false;
    mp_vm_yield_reason = YIELD_BUDGET;

    mp_code_state_t *entry_state;

    if (vm->first_entry) {
        vm->first_entry = false;
        mp_vm_yield_state = NULL;
        entry_state = vm->code_state;
    } else if (mp_vm_yield_state != NULL) {
        /* Resume at the innermost frame that was active when VM yielded. */
        entry_state = (mp_code_state_t *)mp_vm_yield_state;
        mp_vm_yield_state = NULL;
    } else {
        entry_state = vm->code_state;
    }

    mp_vm_return_kind_t ret = mp_execute_bytecode(entry_state, MP_OBJ_NULL);

    switch (ret) {
        case MP_VM_RETURN_NORMAL:
            mp_globals_set(vm->code_state->old_globals);
            vm->started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(vm->code_state);
            #endif
            vm->code_state = NULL;
            return 0;

        case MP_VM_RETURN_YIELD:
            // Python-level `yield` expression (generator yielded a value).
            // Supervisor treats it identically to SUSPEND: code is paused,
            // resume next frame.
            return 1;

        case MP_VM_RETURN_SUSPEND:
            // Supervisor-driven suspension (budget exhausted, etc).
            // code_state is preserved on pystack; next cp_step resumes.
            return 1;

        case MP_VM_RETURN_EXCEPTION: {
            mp_obj_t exc = MP_OBJ_FROM_PTR(vm->code_state->state[0]);

            // Identity-check the suspend sentinel BEFORE the SystemExit
            // subclass check — the sentinel is a static singleton raised by
            // fun_bc_call / gen_resume_and_raise to unwind the C stack when
            // the supervisor suspended a C-called Python frame.  State is
            // preserved in mp_vm_yield_state; next cp_step resumes normally.
            if (exc == MP_OBJ_FROM_PTR(&mp_vm_suspend_sentinel)) {
                return 1;
            }

            if (mp_obj_is_subclass_fast(
                    MP_OBJ_FROM_PTR(((mp_obj_base_t *)MP_OBJ_TO_PTR(exc))->type),
                    MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
                vm->started = false;
                #if MICROPY_ENABLE_PYSTACK
                mp_pystack_free(vm->code_state);
                #endif
                vm->code_state = NULL;
                return 0;
            }
            mp_obj_print_exception(&mp_stderr_print, exc);
            mp_globals_set(vm->code_state->old_globals);
            vm->started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(vm->code_state);
            #endif
            vm->code_state = NULL;
            return 2;
        }

        default:
            return 2;
    }
}

void vm_yield_stop(void) {
    cp_context_vm_t *vm = cp_context_vm(cp_context_active());
    if (vm->started && vm->code_state != NULL) {
        #if MICROPY_ENABLE_PYSTACK
        mp_pystack_free(vm->code_state);
        #endif
    }
    vm->code_state = NULL;
    vm->started = false;
    cp_context_set_delay(cp_context_active(), 0);
    _yield_requested = false;
}
