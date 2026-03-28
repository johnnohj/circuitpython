/*
 * wasm_supervisor.c — WASM supervisor that owns the Python VM cycle.
 *
 * On a real board, hardware exists first and the supervisor initializes
 * it.  In the browser, the webpage and JS are the hardware — already
 * running.  This supervisor calls mp_init() and owns the Python VM:
 *
 *   - Two Python VMs: REPL and code.py
 *   - Switching between them (code.py finishes → REPL, Ctrl+D → restart)
 *   - Frame budget enforcement (~13ms per cp_step)
 *   - port_background_task(): things Python needs WASM/JS to do
 *
 * CLI mode (main): blocking REPL via pyexec_friendly_repl().
 *
 * Browser mode (cp_step): yield-driven stepping.  The REPL calls
 * mp_hal_stdin_rx_chr() which yields when the rx buffer is empty.
 * code.py yields at backwards branches when the wall-clock budget
 * is spent.  Both use MICROPY_VM_YIELD_ENABLED + pystack so all
 * Python state lives on the heap, not the C stack.
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

#if CIRCUITPY_DISPLAYIO
#include "board_display.h"
#endif

#include "supervisor/hal.h"
#if CIRCUITPY_STATUS_BAR
#include "supervisor/shared/status_bar.h"
#endif
#include "mpthreadport.h"

/* ------------------------------------------------------------------ */
/* Forward declarations — vm_yield.c                                   */
/* ------------------------------------------------------------------ */

#if MICROPY_VM_YIELD_ENABLED
extern void vm_yield_set_frame_start(uint64_t ms);
extern void vm_yield_set_budget(uint32_t ms);
extern int mp_hal_delay_active(void);
extern int vm_yield_start_file(const char *path);
extern int vm_yield_step(void);
extern void vm_yield_stop(void);
extern bool vm_yield_code_running(void);
extern void mp_vm_request_yield(int reason, uint32_t arg);
#endif

/* mp_stderr_print is in wasi_mphal.c */

/* ------------------------------------------------------------------ */
/* Supervisor state (declared early for supervisor_execution_status)    */
/* ------------------------------------------------------------------ */

typedef enum {
    SUP_UNINITIALIZED = 0,
    SUP_REPL,           /* REPL active (blocking in CLI, yielding in browser) */
    SUP_CODE_RUNNING,   /* code.py executing */
    SUP_CODE_FINISHED,  /* code.py done, switching to REPL */
} sup_state_t;

static sup_state_t _state = SUP_UNINITIALIZED;
static uint32_t _frame_count = 0;
static uint64_t _frame_start_ms = 0;

/* Runtime mode: CLI (blocking stdin) vs browser (yield-driven). */
bool wasm_cli_mode = false;

/* ------------------------------------------------------------------ */
/* supervisor_execution_status — called by status_bar.c                */
/* ------------------------------------------------------------------ */

#if CIRCUITPY_STATUS_BAR
#include "supervisor/shared/serial.h"

void supervisor_execution_status(void) {
    switch (_state) {
        case SUP_REPL:
            serial_write("REPL");
            break;
        case SUP_CODE_RUNNING:
            serial_write("code.py");
            break;
        case SUP_CODE_FINISHED:
            serial_write("Done");
            break;
        default:
            serial_write("...");
            break;
    }
}
#endif

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

/* ------------------------------------------------------------------ */
/* Background tasks — called from MICROPY_VM_HOOK_LOOP (vm_yield.c)    */
/*                                                                     */
/* "Things Python needs the platform to do to keep going."             */
/* ------------------------------------------------------------------ */

void wasm_background_tasks(void) {
    /* Check for Ctrl-C in rx buffer */
    for (int i = _rx_tail; i < _rx_head; i++) {
        if (_rx_buf[i] == 3) {
            mp_sched_keyboard_interrupt();
            break;
        }
    }

    /* Future: check MEMFS hw endpoints, cursor blink, display dirty */
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
/* Core init — called once, sets up the VM                             */
/* ------------------------------------------------------------------ */

static void _core_init(void) {
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(heap, heap + WASM_GC_HEAP_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    mp_pystack_init(pystack_buf,
                    pystack_buf + WASM_PYSTACK_SIZE / sizeof(mp_obj_t));
    #endif

    /* Open /hal/ fd endpoints before mp_init() — hardware must be
     * available before Python starts. */
    hal_init();

    mp_init();

    #if MICROPY_VFS_POSIX
    {
        /* Mount VfsPosix("/CIRCUITPY") at "/" so Python sees /code.py etc.
         * but the underlying WASI paths are /CIRCUITPY/code.py — matching
         * the IdbBackend persistence prefix and real-board drive name. */
        mp_obj_t root_arg = mp_obj_new_str_via_qstr("/CIRCUITPY", 10);
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(
                &mp_type_vfs_posix, 1, 0, &root_arg),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    #endif

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

    #if MICROPY_PY_SYS_ARGV
    {
        mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);
    }
    #endif

    /* Initialize the display + supervisor terminal (Blinka logo + REPL).
     * Must come after mp_init() and GC setup — allocates displayio objects. */
    #if CIRCUITPY_DISPLAYIO
    board_display_init();
    #endif

    /* Initialize and start the status bar (renders to terminal top row). */
    #if CIRCUITPY_STATUS_BAR
    supervisor_status_bar_init();
    supervisor_status_bar_start();
    #endif
}

/* ------------------------------------------------------------------ */
/* _start / main — CLI mode                                            */
/*                                                                     */
/* Standard WASI entry point for wasmtime/node testing.                */
/* The supervisor runs the blocking REPL, restarting on Ctrl+D.        */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    wasm_cli_mode = true;
    _core_init();
    _state = SUP_REPL;

    fprintf(stderr, "[sup] CircuitPython WASM (heap=%dK)\n",
            WASM_GC_HEAP_SIZE / 1024);

    /* Board lifecycle loop: run code.py (if exists) then REPL,
     * restart on Ctrl+D.  For now, just REPL. */
    #if MICROPY_REPL_EVENT_DRIVEN
    /* Event-driven REPL in CLI mode: simulate blocking by reading
     * one character at a time from stdin and feeding it to the
     * event-driven state machine. */
    pyexec_event_repl_init();
    for (;;) {
        int c = mp_hal_stdin_rx_chr();
        int ret = pyexec_event_repl_process_char(c);
        if (ret & PYEXEC_FORCED_EXIT) {
            break;
        }
    }
    #else
    for (;;) {
        int ret = pyexec_friendly_repl();
        if (ret == PYEXEC_FORCED_EXIT) {
            break;
        }
    }
    #endif

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

    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_set_budget(WASM_FRAME_BUDGET_MS);
    #endif

    #if MICROPY_REPL_EVENT_DRIVEN
    pyexec_event_repl_init();
    #endif

    fprintf(stderr, "[sup] initialized (heap=%dK budget=%dms)\n",
            WASM_GC_HEAP_SIZE / 1024, WASM_FRAME_BUDGET_MS);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Exported: cp_step — one frame of supervisor work                    */
/*                                                                     */
/* Called once per rAF (~60fps).  Five phases:                          */
/*                                                                     */
/*   1. HAL STEP — drive simulated hardware (poll /hal/ endpoints)     */
/*   2. TICK CATCHUP + CALLBACKS — simulate elapsed ms, drain queue    */
/*   3. PYTHON VM — REPL or code.py until budget spent                 */
/*   4. POST-VM CALLBACKS — drain queue one more time                  */
/*   5. HAL EXPORT — flush hw_state changes to /hal/ endpoints         */
/*                                                                     */
/* On a real board, hardware runs itself.  On WASM, nothing happens    */
/* unless the supervisor makes it happen.  The HAL step (phase 1)      */
/* replaces real hardware — it advances the simulation so Python sees  */
/* fresh data.                                                         */
/* ------------------------------------------------------------------ */

/* Forward declarations — supervisor/hal.c */
extern void hal_step(void);
extern void hal_export_dirty(void);

__attribute__((export_name("cp_step")))
int cp_step(void) {
    if (_state == SUP_UNINITIALIZED) {
        cp_init();
    }

    _frame_start_ms = (uint64_t)mp_hal_ticks_ms();
    _frame_count++;

    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_set_frame_start(_frame_start_ms);
    #endif

    /* ── Phase 1: HAL step ── */
    hal_step();

    /* ── Phase 2: Tick catchup + background callbacks ── */
    /* background_callback_run_all() calls port_background_task() first
     * (which simulates elapsed ms via supervisor_tick), then drains
     * the callback queue (supervisor_background_tick, etc). */
    RUN_BACKGROUND_TASKS;

    /* ── Phase 3: Python VM ── */
    switch (_state) {

    case SUP_REPL: {
        #if MICROPY_REPL_EVENT_DRIVEN
        /* Event-driven REPL: feed queued keystrokes one at a time.
         * pyexec_event_repl_process_char is a pure state machine —
         * no blocking.  When the user hits Enter, it compiles and
         * executes inline.  If execution triggers VM yield (budget
         * expired), it returns and we resume next frame. */
        while (_rx_available() > 0) {
            unsigned char c = _rx_buf[_rx_tail++];
            if (_rx_tail >= _rx_head) {
                _rx_head = 0;
                _rx_tail = 0;
            }
            if (c == '\n') {
                c = '\r';
            }
            int ret = pyexec_event_repl_process_char(c);
            if (ret & PYEXEC_FORCED_EXIT) {
                /* Ctrl-D: restart REPL */
                pyexec_event_repl_init();
            }
        }
        #endif
        break;
    }

    case SUP_CODE_RUNNING: {
        #if MICROPY_VM_YIELD_ENABLED
        int ret = vm_yield_step();
        if (ret == 0) {
            _state = SUP_CODE_FINISHED;
        } else if (ret == 2) {
            _state = SUP_CODE_FINISHED;
        }
        /* ret == 1: yielded, will resume next frame */
        #endif
        break;
    }

    case SUP_CODE_FINISHED:
        _state = SUP_REPL;
        fprintf(stderr, "[sup] code.py finished → REPL\n");
        break;

    default:
        break;
    }

    /* ── Phase 4: Post-VM background callbacks ── */
    RUN_BACKGROUND_TASKS;

    /* ── Phase 5: HAL export ── */
    hal_export_dirty();

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
    #if MICROPY_VM_YIELD_ENABLED
    fprintf(stderr, "[sup] run_file: %s\n", path);
    int ret = vm_yield_start_file(path);
    if (ret == 0) {
        _state = SUP_CODE_RUNNING;
    }
    return ret;
    #else
    (void)path;
    fprintf(stderr, "[sup] run_file: VM yield not enabled\n");
    return -1;
    #endif
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

#if MICROPY_VM_YIELD_ENABLED
__attribute__((export_name("cp_get_yield_reason")))
int cp_get_yield_reason(void) {
    extern volatile int mp_vm_yield_reason;
    return mp_vm_yield_reason;
}

__attribute__((export_name("cp_get_yield_arg")))
uint32_t cp_get_yield_arg(void) {
    extern volatile uint32_t mp_vm_yield_arg;
    return mp_vm_yield_arg;
}
#endif

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
