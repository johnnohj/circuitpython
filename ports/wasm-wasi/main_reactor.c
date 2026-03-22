/*
 * WASI reactor entry point — externally-driven CircuitPython execution.
 *
 * Instead of _start running to completion, the binary exports:
 *   mp_runtime_init()    — initialize GC, VFS, sys.path
 *   mp_vm_start(src)     — compile source, prepare code_state for stepping
 *   mp_vm_step()         — execute one burst of bytecodes, return status
 *   mp_vm_yield_reason   — why the VM yielded (sleep, show, budget, etc.)
 *   mp_vm_yield_arg      — argument for the yield (e.g., sleep duration ms)
 *
 * The JS host calls these in a requestAnimationFrame loop, acting as
 * the supervisor. Hardware state lives at fixed offsets in linear memory
 * that JS reads directly for rendering.
 *
 * Adapted from ports/wasm/main.c mp_vm_start/mp_vm_step.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/cstack.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/stackctrl.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "genhdr/mpversion.h"

// ---- Yield protocol ----
// Written by common_hal_* functions before requesting yield.
// Read by JS host to determine what to do between steps.

enum {
    YIELD_BUDGET  = 0,  // step budget exhausted (normal yield)
    YIELD_SLEEP   = 1,  // time.sleep() — arg is duration in ms
    YIELD_SHOW    = 2,  // pixels.show() or display refresh
    YIELD_IO_WAIT = 3,  // waiting for hardware I/O
};

volatile int mp_vm_yield_reason = YIELD_BUDGET;
volatile uint32_t mp_vm_yield_arg = 0;

// ---- Yield budget (replaces JS mp_vm_should_yield) ----

static int _yield_budget = 256;

void mp_vm_set_budget(int n) {
    _yield_budget = n;
}

int mp_vm_should_yield(void) {
    if (--_yield_budget <= 0) {
        mp_vm_yield_reason = YIELD_BUDGET;
        return 1;
    }
    return 0;
}

// Request immediate yield with a specific reason
void mp_vm_request_yield(int reason, uint32_t arg) {
    mp_vm_yield_reason = reason;
    mp_vm_yield_arg = arg;
    _yield_budget = 0;  // next should_yield check will trigger
}

// ---- Delay as yield ----

static uint64_t _delay_until = 0;

int mp_hal_delay_active(void) {
    if (_delay_until == 0) {
        return 0;
    }
    if (mp_hal_ticks_ms() >= _delay_until) {
        _delay_until = 0;
        return 0;
    }
    return 1;
}

// Override mp_hal_delay_ms: instead of blocking, yield to host
void mp_hal_delay_ms(mp_uint_t ms) {
    if (ms == 0) {
        return;
    }
    _delay_until = mp_hal_ticks_ms() + ms;
    mp_vm_request_yield(YIELD_SLEEP, (uint32_t)ms);
}

// ---- GC heap ----

#ifndef MICROPY_GC_HEAP_SIZE
#define MICROPY_GC_HEAP_SIZE (512 * 1024)
#endif

static char heap[MICROPY_GC_HEAP_SIZE];

// ---- Stepping state ----
// Persistent between mp_vm_start() and mp_vm_step() calls.

static mp_code_state_t *_vm_step_code_state = NULL;
static mp_obj_t _vm_step_module_fun = MP_OBJ_NULL;
static bool _vm_step_started = false;
static bool _vm_step_first_entry = false;

// Written by the VM yield handler (MICROPY_VM_YIELD_SAVE_STATE in vm.c).
// Contains the innermost code_state at the point of yield.
void *mp_vm_yield_state = NULL;

// ---- Exported functions ----
// __attribute__((export_name("..."))) makes them visible in the WASM exports.

__attribute__((export_name("mp_runtime_init")))
void mp_runtime_init(void) {
    mp_cstack_init_with_sp_here(64000);
    gc_init(heap, heap + MICROPY_GC_HEAP_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    static mp_obj_t pystack[2048];
    mp_pystack_init(pystack, &pystack[MP_ARRAY_SIZE(pystack)]);
    #endif

    mp_init();

    #if MICROPY_VFS_POSIX
    {
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    #endif
}

/*
 * Compile source code and prepare for stepping.
 * src_path: path to a .py file (must be accessible via WASI preopens)
 * Returns: 0 on success, -1 on compile error
 */
__attribute__((export_name("mp_vm_start")))
int mp_vm_start(const char *src_path) {
    // Reset previous state
    _vm_step_code_state = NULL;
    _vm_step_module_fun = MP_OBJ_NULL;
    _vm_step_started = false;
    _delay_until = 0;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str(src_path));
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        _vm_step_module_fun = mp_compile(&parse_tree, source_name, false);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_stderr_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        return -1;
    }

    // Prepare code_state on pystack
    _vm_step_code_state = mp_obj_fun_bc_prepare_codestate(
        _vm_step_module_fun, 0, 0, NULL);
    if (_vm_step_code_state == NULL) {
        fprintf(stderr, "cannot create code state for stepping\n");
        return -1;
    }
    _vm_step_code_state->prev = NULL;
    _vm_step_started = true;
    _vm_step_first_entry = true;
    mp_vm_yield_state = NULL;

    return 0;
}

/*
 * Execute one burst of bytecodes.
 *
 * Returns:
 *   0 — normal completion (code finished)
 *   1 — yielded (call mp_vm_step again, after checking yield_reason)
 *   2 — exception (traceback printed to stderr)
 */
__attribute__((export_name("mp_vm_step")))
int mp_vm_step(void) {
    if (!_vm_step_started || _vm_step_code_state == NULL) {
        return 0;
    }

    // Reset budget for this step
    _yield_budget = 256;
    mp_vm_yield_reason = YIELD_BUDGET;

    mp_code_state_t *entry_state;

    if (_vm_step_first_entry) {
        _vm_step_first_entry = false;
        mp_vm_yield_state = NULL;
        entry_state = _vm_step_code_state;
    } else if (mp_vm_yield_state != NULL) {
        // Resume at the innermost frame that was active when VM yielded.
        // Critical for stackless mode: yield may fire deep in a call chain.
        entry_state = (mp_code_state_t *)mp_vm_yield_state;
        mp_vm_yield_state = NULL;
    } else {
        entry_state = _vm_step_code_state;
    }

    mp_vm_return_kind_t ret = mp_execute_bytecode(entry_state, MP_OBJ_NULL);

    switch (ret) {
        case MP_VM_RETURN_NORMAL:
            mp_globals_set(_vm_step_code_state->old_globals);
            _vm_step_started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_vm_step_code_state);
            #endif
            _vm_step_code_state = NULL;
            return 0;

        case MP_VM_RETURN_YIELD:
            return 1;

        case MP_VM_RETURN_EXCEPTION: {
            mp_obj_t exc = MP_OBJ_FROM_PTR(_vm_step_code_state->state[0]);
            // Check for SystemExit
            if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(((mp_obj_base_t *)MP_OBJ_TO_PTR(exc))->type),
                                        MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
                _vm_step_started = false;
                #if MICROPY_ENABLE_PYSTACK
                mp_pystack_free(_vm_step_code_state);
                #endif
                _vm_step_code_state = NULL;
                return 0;
            }
            mp_obj_print_exception(&mp_stderr_print, exc);
            mp_globals_set(_vm_step_code_state->old_globals);
            _vm_step_started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_vm_step_code_state);
            #endif
            _vm_step_code_state = NULL;
            return 2;
        }

        default:
            return 2;
    }
}

// ---- Exported accessors for JS ----

__attribute__((export_name("mp_vm_get_yield_reason")))
int mp_vm_get_yield_reason(void) {
    return mp_vm_yield_reason;
}

__attribute__((export_name("mp_vm_get_yield_arg")))
uint32_t mp_vm_get_yield_arg(void) {
    return mp_vm_yield_arg;
}

__attribute__((export_name("mp_hal_delay_active")))
int mp_hal_delay_active_export(void) {
    return mp_hal_delay_active();
}

// ---- Tick stub ----
// The reactor variant doesn't use tick.c — the JS host drives timing.
// supervisor_tick is called by port_background_task but is a no-op here.
void supervisor_tick(void) {}
uint32_t supervisor_ticks_ms32(void) { return (uint32_t)mp_hal_ticks_ms(); }
uint64_t supervisor_ticks_ms64(void) { return mp_hal_ticks_ms(); }

// ---- Required stubs ----

void nlr_jump_fail(void *val) {
    (void)val;
    fprintf(stderr, "FATAL: uncaught NLR\n");
    __builtin_trap();
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)func;
    fprintf(stderr, "Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __builtin_trap();
}
#endif
