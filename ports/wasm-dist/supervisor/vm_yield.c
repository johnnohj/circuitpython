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

#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "extmod/vfs.h"

#include "mpthreadport.h"

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

void vm_yield_set_frame_start(uint64_t ms) {
    _frame_start_ms = ms;
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

void wasm_vm_hook_loop(void) {
    /* 1. Run port background tasks */
    wasm_background_tasks();

    /* 2. Check wall-clock budget (skip in CLI mode and atomic sections).
     *    CLI mode uses blocking I/O — no frame budget, no yield.
     *    Browser mode yields when the frame budget is exhausted. */
    if (!wasm_cli_mode && !_yield_requested && !mp_thread_in_atomic_section()) {
        uint64_t now = (uint64_t)mp_hal_ticks_ms();
        if (now - _frame_start_ms >= _frame_budget_ms) {
            mp_vm_request_yield(YIELD_BUDGET, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Delay-as-yield                                                      */
/*                                                                     */
/* time.sleep() doesn't block — it sets a target time and yields.      */
/* The supervisor checks mp_hal_delay_active() each frame and skips    */
/* VM execution until the sleep expires.                               */
/* ------------------------------------------------------------------ */

static uint64_t _delay_until = 0;

int mp_hal_delay_active(void) {
    if (_delay_until == 0) {
        return 0;
    }
    if ((uint64_t)mp_hal_ticks_ms() >= _delay_until) {
        _delay_until = 0;
        return 0;
    }
    return 1;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    if (ms == 0) {
        return;
    }
    _delay_until = (uint64_t)mp_hal_ticks_ms() + ms;
    mp_vm_request_yield(YIELD_SLEEP, (uint32_t)ms);
}

/* ------------------------------------------------------------------ */
/* Stepping state — persistent between cp_step() calls                 */
/* ------------------------------------------------------------------ */

static mp_code_state_t *_vm_code_state = NULL;
static mp_obj_t _vm_module_fun = MP_OBJ_NULL;
static bool _vm_started = false;
static bool _vm_first_entry = false;

bool vm_yield_code_running(void) {
    return _vm_started;
}

/*
 * Compile a .py file and prepare for stepping.
 * Returns 0 on success, -1 on compile error.
 */
int vm_yield_start_file(const char *path) {
    _vm_code_state = NULL;
    _vm_module_fun = MP_OBJ_NULL;
    _vm_started = false;
    _delay_until = 0;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str(path));
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        _vm_module_fun = mp_compile(&parse_tree, source_name, false);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_stderr_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        return -1;
    }

    _vm_code_state = mp_obj_fun_bc_prepare_codestate(
        _vm_module_fun, 0, 0, NULL);
    if (_vm_code_state == NULL) {
        fprintf(stderr, "[vm_yield] cannot create code state\n");
        return -1;
    }
    _vm_code_state->prev = NULL;
    _vm_started = true;
    _vm_first_entry = true;
    mp_vm_yield_state = NULL;

    return 0;
}

/*
 * Execute one burst of bytecodes for the file started by vm_yield_start_file.
 *
 * Returns:
 *   0 — normal completion (code finished)
 *   1 — yielded (call again next frame)
 *   2 — exception (traceback printed to stderr)
 */
int vm_yield_step(void) {
    if (!_vm_started || _vm_code_state == NULL) {
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

    if (_vm_first_entry) {
        _vm_first_entry = false;
        mp_vm_yield_state = NULL;
        entry_state = _vm_code_state;
    } else if (mp_vm_yield_state != NULL) {
        /* Resume at the innermost frame that was active when VM yielded. */
        entry_state = (mp_code_state_t *)mp_vm_yield_state;
        mp_vm_yield_state = NULL;
    } else {
        entry_state = _vm_code_state;
    }

    mp_vm_return_kind_t ret = mp_execute_bytecode(entry_state, MP_OBJ_NULL);

    switch (ret) {
        case MP_VM_RETURN_NORMAL:
            mp_globals_set(_vm_code_state->old_globals);
            _vm_started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_vm_code_state);
            #endif
            _vm_code_state = NULL;
            return 0;

        case MP_VM_RETURN_YIELD:
            return 1;

        case MP_VM_RETURN_EXCEPTION: {
            mp_obj_t exc = MP_OBJ_FROM_PTR(_vm_code_state->state[0]);
            if (mp_obj_is_subclass_fast(
                    MP_OBJ_FROM_PTR(((mp_obj_base_t *)MP_OBJ_TO_PTR(exc))->type),
                    MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
                _vm_started = false;
                #if MICROPY_ENABLE_PYSTACK
                mp_pystack_free(_vm_code_state);
                #endif
                _vm_code_state = NULL;
                return 0;
            }
            mp_obj_print_exception(&mp_stderr_print, exc);
            mp_globals_set(_vm_code_state->old_globals);
            _vm_started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_vm_code_state);
            #endif
            _vm_code_state = NULL;
            return 2;
        }

        default:
            return 2;
    }
}

void vm_yield_stop(void) {
    if (_vm_started && _vm_code_state != NULL) {
        #if MICROPY_ENABLE_PYSTACK
        mp_pystack_free(_vm_code_state);
        #endif
    }
    _vm_code_state = NULL;
    _vm_module_fun = MP_OBJ_NULL;
    _vm_started = false;
    _delay_until = 0;
    _yield_requested = false;
}
