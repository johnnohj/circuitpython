/*
 * wasm_supervisor.c — WASM supervisor that owns the Python VM cycle.
 *
 * On a real board, hardware exists first and the supervisor initializes
 * it.  In the browser, the webpage and JS are the hardware — already
 * running.  This supervisor calls mp_init() and owns the Python VM:
 *
 *   - Two Python VMs: REPL and code.py
 *   - Switching between them (code.py finishes → REPL, Ctrl+D → restart)
 *   - Frame budget enforcement (~12-14ms per cp_step)
 *   - port_background_task(): things Python needs WASM/JS to do
 *
 * The REPL uses the standard blocking pyexec_friendly_repl().  It calls
 * mp_hal_stdin_rx_chr() which blocks until input is available.  In CLI
 * mode (wasmtime/node), this is a blocking read(2) on stdin.  In the
 * browser variant, the VM yield machinery suspends and resumes across
 * frames — the supervisor yields when stdin is empty and the budget is
 * spent, and resumes the REPL where it left off next frame.
 *
 * code.py uses the same mechanism: compile, enter mp_execute_bytecode,
 * yield at backwards branches when budget exhausted, resume next frame.
 *
 * The supervisor does NOT use MICROPY_REPL_EVENT_DRIVEN.  The blocking
 * REPL handles all edge cases (paste mode, history, multiline) and the
 * yield machinery makes it cooperative transparently.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/cstack.h"
#include "py/mphal.h"
#include "py/repl.h"
#include "shared/runtime/pyexec.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"

#if MICROPY_ENABLE_PYSTACK
#include "py/pystack.h"
#endif

#include "mpthreadport.h"

/* ------------------------------------------------------------------ */
/* stderr printer — referenced by py/ for debug/error output           */
/* ------------------------------------------------------------------ */

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    write(STDERR_FILENO, str, len);
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#ifndef WASM_GC_HEAP_SIZE
#define WASM_GC_HEAP_SIZE (512 * 1024)
#endif

#ifndef WASM_PYSTACK_SIZE
#define WASM_PYSTACK_SIZE (8 * 1024)
#endif

/* Frame budget: how many ms the supervisor gets per cp_step() call. */
#ifndef WASM_FRAME_BUDGET_MS
#define WASM_FRAME_BUDGET_MS 13
#endif

/* ------------------------------------------------------------------ */
/* Static buffers                                                      */
/* ------------------------------------------------------------------ */

static char heap[WASM_GC_HEAP_SIZE];

#if MICROPY_ENABLE_PYSTACK
static mp_obj_t pystack_buf[WASM_PYSTACK_SIZE / sizeof(mp_obj_t)];
#endif

/* ------------------------------------------------------------------ */
/* Supervisor state                                                    */
/* ------------------------------------------------------------------ */

typedef enum {
    SUP_UNINITIALIZED = 0,
    SUP_REPL,           /* blocking REPL active */
    SUP_CODE_RUNNING,   /* code.py executing */
    SUP_CODE_SLEEPING,  /* code.py called time.sleep() */
    SUP_CODE_FINISHED,  /* code.py done, switching to REPL */
} sup_state_t;

static sup_state_t _state = SUP_UNINITIALIZED;
static uint32_t _frame_count = 0;
static uint64_t _frame_start_ms = 0;
static uint64_t _sleep_until_ms = 0;

/* ------------------------------------------------------------------ */
/* Keyboard input buffer                                               */
/*                                                                     */
/* JS pushes bytes via cp_push_key().  mp_hal_stdin_rx_chr() reads     */
/* from here.  In CLI mode, _rx_refill() reads from WASI stdin.       */
/* ------------------------------------------------------------------ */

#define RX_BUF_SIZE 256
uint8_t _rx_buf[RX_BUF_SIZE];
int _rx_head = 0;
int _rx_tail = 0;

int _rx_available(void) {
    return _rx_head - _rx_tail;
}

static void _rx_reset(void) {
    _rx_head = 0;
    _rx_tail = 0;
}

/* ------------------------------------------------------------------ */
/* Frame budget                                                        */
/* ------------------------------------------------------------------ */

bool wasm_budget_exhausted(void) {
    if (mp_thread_in_atomic_section()) {
        return false;
    }
    uint64_t now = (uint64_t)mp_hal_ticks_ms();
    return (now - _frame_start_ms) >= WASM_FRAME_BUDGET_MS;
}

uint64_t wasm_frame_start_ms(void) {
    return _frame_start_ms;
}

/* ------------------------------------------------------------------ */
/* port_background_task — things Python needs the platform to do       */
/*                                                                     */
/* Called during RUN_BACKGROUND_TASKS (when wired up).                 */
/* Checks MEMFS endpoints, Ctrl-C, budget.                             */
/* ------------------------------------------------------------------ */

static void _port_background_task(void) {
    /* Check for Ctrl-C in rx buffer */
    for (int i = _rx_tail; i < _rx_head; i++) {
        if (_rx_buf[i] == 3) {
            mp_sched_keyboard_interrupt();
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Core init — called once, sets up the VM                             */
/* ------------------------------------------------------------------ */

static void _core_init(void) {
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(heap, heap + WASM_GC_HEAP_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    mp_pystack_init(pystack_buf,
                    pystack_buf + WASM_PYSTACK_SIZE / sizeof(mp_obj_t));
    #endif

    mp_init();

    #if MICROPY_VFS_POSIX
    {
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(
                &mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    #endif

    /* Set up sys.path — must be a proper list, not the default string.
     * The unix port parses MICROPYPATH; we just set sensible defaults. */
    #if MICROPY_PY_SYS_PATH
    {
        mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_path), 0);
        mp_obj_list_append(mp_sys_path,
                           MP_OBJ_NEW_QSTR(qstr_from_str(".frozen")));
        mp_obj_list_append(mp_sys_path,
                           MP_OBJ_NEW_QSTR(qstr_from_str("/lib")));
        mp_obj_list_append(mp_sys_path,
                           MP_OBJ_NEW_QSTR(qstr_from_str(".")));
    }
    #endif

    /* Set up sys.argv */
    #if MICROPY_PY_SYS_ARGV
    {
        mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);
    }
    #endif
}

/* ------------------------------------------------------------------ */
/* REPL runner                                                         */
/*                                                                     */
/* Calls the standard blocking REPL. mp_hal_stdin_rx_chr() reads from  */
/* the rx buffer; in CLI mode it blocks on WASI stdin.  In the browser */
/* variant (future), the VM yield machinery will suspend here when the  */
/* rx buffer is empty and the budget is spent, resuming next frame.     */
/* ------------------------------------------------------------------ */

static int _run_repl(void) {
    /* pyexec_friendly_repl returns when Ctrl+D is pressed */
    return pyexec_friendly_repl();
}

/* ------------------------------------------------------------------ */
/* _start / main — CLI mode                                            */
/*                                                                     */
/* Standard WASI entry point for wasmtime/node testing.                */
/* The supervisor runs the REPL in a loop, restarting on Ctrl+D.       */
/* This is the real board lifecycle: code.py → REPL → restart.         */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    _core_init();
    _state = SUP_REPL;

    fprintf(stderr, "[sup] CircuitPython WASM (heap=%dK)\n",
            WASM_GC_HEAP_SIZE / 1024);

    /* Board lifecycle loop: run code.py (if exists) then REPL,
     * restart on Ctrl+D.  For now, just REPL. */
    for (;;) {
        int ret = _run_repl();

        if (ret == PYEXEC_FORCED_EXIT) {
            /* Ctrl+D in REPL — on a real board this restarts code.py.
             * For CLI mode, exit. */
            break;
        }
    }

    mp_deinit();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Exported: cp_init — browser variant init                            */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_init")))
int cp_init(void) {
    if (_state != SUP_UNINITIALIZED) return 0;

    _core_init();
    _state = SUP_REPL;

    fprintf(stderr, "[sup] initialized (heap=%dK budget=%dms)\n",
            WASM_GC_HEAP_SIZE / 1024, WASM_FRAME_BUDGET_MS);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Exported: cp_step — one frame of supervisor work                    */
/*                                                                     */
/* The browser variant entry point.  Called once per rAF (~60fps).     */
/* The supervisor spends its budget, then returns.                     */
/*                                                                     */
/* In the browser variant (with MICROPY_VM_YIELD_ENABLED), this will:  */
/*   1. Run background tasks                                           */
/*   2. Resume the VM (REPL or code.py) until budget exhausted         */
/*   3. Return to JS for canvas paint + event handling                 */
/*                                                                     */
/* The VM yield saves code_state on pystack and returns                */
/* MP_VM_RETURN_YIELD.  Next cp_step() resumes at the saved state.     */
/* mp_hal_stdin_rx_chr() yields when the rx buffer is empty.           */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_step")))
int cp_step(void) {
    if (_state == SUP_UNINITIALIZED) {
        cp_init();
    }

    _frame_start_ms = (uint64_t)mp_hal_ticks_ms();
    _frame_count++;

    _port_background_task();

    switch (_state) {

    case SUP_REPL:
        /* Browser variant: resume the blocking REPL.
         * The VM yield machinery suspends when:
         *   - Budget exhausted (backwards branch check)
         *   - stdin empty (mp_hal_stdin_rx_chr yield)
         *
         * TODO: wire up VM yield for REPL resume.
         * For now, cp_step is a placeholder — the CLI path
         * (main) runs the blocking REPL directly. */
        break;

    case SUP_CODE_RUNNING:
        /* Resume code.py bytecodes until budget exhausted.
         * TODO: integrate vm_yield.c pattern */
        break;

    case SUP_CODE_SLEEPING:
        if ((uint64_t)mp_hal_ticks_ms() >= _sleep_until_ms) {
            _sleep_until_ms = 0;
            _state = SUP_CODE_RUNNING;
        }
        break;

    case SUP_CODE_FINISHED:
        _state = SUP_REPL;
        fprintf(stderr, "[sup] code.py finished → REPL\n");
        break;

    default:
        break;
    }

    _port_background_task();

    return (int)_state;
}

/* ------------------------------------------------------------------ */
/* Exported: cp_push_key                                               */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_push_key")))
void cp_push_key(int c) {
    if (_rx_head < RX_BUF_SIZE) {
        _rx_buf[_rx_head++] = (uint8_t)c;
    }
}

/* ------------------------------------------------------------------ */
/* Exported: cp_run_file                                               */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_run_file")))
int cp_run_file(const char *path) {
    fprintf(stderr, "[sup] run_file: %s\n", path);
    _state = SUP_CODE_RUNNING;
    _sleep_until_ms = 0;
    /* TODO: compile file, prepare code_state for stepping */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Exported: accessors                                                 */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_get_state")))
int cp_get_state(void) {
    return (int)_state;
}

__attribute__((export_name("cp_get_frame_count")))
uint32_t cp_get_frame_count(void) {
    return _frame_count;
}

/* ------------------------------------------------------------------ */
/* Required stubs                                                      */
/* ------------------------------------------------------------------ */

void nlr_jump_fail(void *val) {
    fprintf(stderr, "FATAL: nlr_jump_fail(%p)\n", val);
    exit(1);
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line,
                           const char *func, const char *expr) {
    (void)func;
    fprintf(stderr, "Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    exit(1);
}
#endif
