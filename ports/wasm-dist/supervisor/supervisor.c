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
#include "py/bc.h"
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
#include "supervisor/semihosting.h"
#include "wasm_framebuffer.h"
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
extern int vm_yield_start_expr(mp_obj_t module_fun);
extern int vm_yield_step(void);
extern void vm_yield_stop(void);
extern bool vm_yield_code_running(void);
extern void mp_vm_request_yield(int reason, uint32_t arg);
extern volatile int mp_vm_yield_reason;
extern volatile uint32_t mp_vm_yield_arg;
extern void *mp_vm_yield_state;
#endif

/* mp_stderr_print is in wasi_mphal.c */

/* ------------------------------------------------------------------ */
/* Supervisor state (declared early for supervisor_execution_status)    */
/* ------------------------------------------------------------------ */

typedef enum {
    SUP_UNINITIALIZED = 0,
    SUP_REPL,           /* waiting for input (browser: JS owns readline) */
    SUP_EXPR_RUNNING,   /* REPL expression executing via vm_yield_step */
    SUP_CODE_RUNNING,   /* code.py executing via vm_yield_step */
    SUP_CODE_FINISHED,  /* code.py done, switching to REPL */
    SUP_FWIP_BUSY,      /* fwip install/remove in progress (JS async) */
} sup_state_t;

/* fwip request flag — set by modfwip.c, checked in cp_step() */
volatile bool fwip_request_pending = false;

static sup_state_t _state = SUP_UNINITIALIZED;
static uint32_t _frame_count = 0;
static uint64_t _frame_start_ms = 0;

/* fwip polling state — tracks last printed status to avoid repeats */
static char _fwip_last_status[FWIP_STATUS_MAX] = {0};

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

/* Shared input buffer — JS writes source text here before calling
 * cp_exec() or cp_continue().  Exported via cp_input_buf_addr(). */
#define INPUT_BUF_SIZE 4096
static char _input_buf[INPUT_BUF_SIZE];

/* ------------------------------------------------------------------ */
/* Keyboard input buffer                                               */
/*                                                                     */
/* sh_on_event(KEY_DOWN) pushes bytes here.  serial_read() reads       */
/* from here.  In CLI mode, serial_read() falls back to WASI stdin.   */
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
    sh_init();

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

    /* Boot sequence: run boot.py if present. */
    {
        pyexec_result_t result;
        pyexec_file_if_exists("/boot.py", &result);
    }

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

    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_set_budget(WASM_FRAME_BUDGET_MS);
    #endif

    /* ── Boot sequence ──
     * 1. Run boot.py if present (board setup, pin aliases, etc.)
     * 2. Try to run code.py — if it exists, start yield-stepping it
     * 3. If no code.py, drop straight into REPL
     *
     * This matches real CircuitPython board behavior:
     *   boot.py → code.py → REPL (when code.py finishes)
     * settings.toml is read on demand by os.getenv(). */
    {
        pyexec_result_t result;
        pyexec_file_if_exists("/boot.py", &result);
    }

    /* Try to start code.py via the yield-stepping machinery.
     * If it exists and compiles, we enter SUP_CODE_RUNNING.
     * If not, we fall through to REPL. */
    #if MICROPY_VM_YIELD_ENABLED
    if (vm_yield_start_file("/code.py") == 0) {
        _state = SUP_CODE_RUNNING;
        fprintf(stderr, "[sup] initialized → code.py (heap=%dK budget=%dms)\n",
                WASM_GC_HEAP_SIZE / 1024, WASM_FRAME_BUDGET_MS);
    } else
    #endif
    {
        _state = SUP_REPL;
        #if MICROPY_REPL_EVENT_DRIVEN
        pyexec_event_repl_init();
        #endif
        fprintf(stderr, "[sup] initialized → REPL (heap=%dK budget=%dms)\n",
                WASM_GC_HEAP_SIZE / 1024, WASM_FRAME_BUDGET_MS);
    }

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
        if (wasm_cli_mode) {
            #if MICROPY_REPL_EVENT_DRIVEN
            /* CLI mode: feed queued keystrokes via pyexec. */
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
                    pyexec_event_repl_init();
                }
            }
            #endif
        }
        /* Browser mode: SUP_REPL is idle — JS owns readline.
         * JS calls cp_exec() when the user hits Enter, which
         * transitions to SUP_EXPR_RUNNING.  Nothing to do here. */

        /* Check if fwip.install() was called */
        if (fwip_request_pending) {
            fwip_request_pending = false;
            _state = SUP_FWIP_BUSY;
            _fwip_last_status[0] = '\0';
            fprintf(stderr, "[sup] → fwip\n");
        }
        break;
    }

    case SUP_EXPR_RUNNING: {
        /* REPL expression executing — same path as code.py.
         * vm_yield_start_expr() was called by cp_exec(). */
        #if MICROPY_VM_YIELD_ENABLED
        int ret = vm_yield_step();
        if (ret == 0 || ret == 2) {
            /* 0 = normal completion, 2 = exception (already printed) */
            _state = SUP_REPL;
            fprintf(stderr, "[sup] expr done → REPL\n");
        }
        /* ret == 1: yielded, will resume next frame */
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
        #if MICROPY_REPL_EVENT_DRIVEN
        if (wasm_cli_mode) {
            pyexec_event_repl_init();
        }
        #endif
        fprintf(stderr, "[sup] code.py finished → REPL\n");
        break;

    case SUP_FWIP_BUSY: {
        /* Pure C polling — no VM, no bytecode.  JS does the async
         * fetch and updates the shared buffer.  We just print status
         * and wait for DONE/ERROR. */
        fwip_buf_t *buf = (fwip_buf_t *)sh_fwip_addr();
        uint8_t st = buf->state;

        /* Print new status messages as they arrive */
        if (buf->status_len > 0 &&
            strncmp(buf->status, _fwip_last_status, FWIP_STATUS_MAX) != 0) {
            mp_printf(&mp_plat_print, "%s\n", buf->status);
            strncpy(_fwip_last_status, buf->status, FWIP_STATUS_MAX - 1);
            _fwip_last_status[FWIP_STATUS_MAX - 1] = '\0';
        }

        if (st == FWIP_STATE_DONE || st == FWIP_STATE_ERROR) {
            if (st == FWIP_STATE_ERROR) {
                mp_printf(&mp_plat_print, "Error: %s\n", buf->status);
            }
            buf->command = FWIP_CMD_NONE;
            buf->state = FWIP_STATE_IDLE;
            _state = SUP_REPL;
            fprintf(stderr, "[sup] fwip done → REPL\n");
        }
        break;
    }

    default:
        break;
    }

    /* ── Phase 4: Post-VM background callbacks ── */
    RUN_BACKGROUND_TASKS;

    /* ── Phase 5: HAL export + state export ── */
    hal_export_dirty();

    {
        uint32_t yr = 0, ya = 0, depth = 0;
        #if MICROPY_VM_YIELD_ENABLED
        yr = (uint32_t)mp_vm_yield_reason;
        ya = (uint32_t)mp_vm_yield_arg;
        #endif
        #if MICROPY_ENABLE_PYSTACK
        depth = (uint32_t)mp_pystack_usage();
        #endif
        sh_export_state((uint32_t)_state, yr, ya, _frame_count, depth);
    }

    /* Update cursor info for JS-side rendering */
    wasm_cursor_info_update();

    return (int)_state;
}

/* ------------------------------------------------------------------ */
/* cp_print — write text through mp_hal stdout (displayio terminal)    */
/*                                                                     */
/* JS calls this to echo typed characters and prompts so they appear   */
/* on the displayio canvas, not just the serial div.                   */
/* Text is read from the shared input buffer.                          */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_print")))
void cp_print(int len) {
    if (len <= 0 || len >= INPUT_BUF_SIZE) {
        return;
    }
    mp_hal_stdout_tx_strn(_input_buf, (size_t)len);
}

/* ------------------------------------------------------------------ */
/* cp_input_buf — shared buffer for JS → C string passing              */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_input_buf_addr")))
uintptr_t cp_input_buf_addr(void) {
    return (uintptr_t)_input_buf;
}

__attribute__((export_name("cp_input_buf_size")))
int cp_input_buf_size(void) {
    return INPUT_BUF_SIZE;
}

/* ------------------------------------------------------------------ */
/* cp_exec — compile and execute a REPL expression (called by JS)      */
/*                                                                     */
/* JS writes source text into the shared input buffer, then calls      */
/* cp_exec(len).  We compile it, hand the module_fun to                */
/* vm_yield_start_expr, and transition to SUP_EXPR_RUNNING.            */
/* vm_yield_step() handles yield/resume from there.                    */
/*                                                                     */
/* Returns:                                                            */
/*   0  — started ok (will run in SUP_EXPR_RUNNING)                    */
/*   1  — compile error (already printed to stderr)                    */
/*   2  — busy (expression or code.py already running)                 */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_exec")))
int cp_exec(int len) {
    if (_state != SUP_REPL) {
        return 2;  /* busy */
    }
    if (len <= 0 || len >= INPUT_BUF_SIZE) {
        return 1;
    }
    _input_buf[len] = '\0';

    #if MICROPY_VM_YIELD_ENABLED
    mp_obj_t module_fun = MP_OBJ_NULL;

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, _input_buf, (size_t)len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_SINGLE_INPUT);
        module_fun = mp_compile(&parse_tree, source_name, true);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
        return 1;  /* compile error */
    }

    if (vm_yield_start_expr(module_fun) != 0) {
        return 1;
    }

    _state = SUP_EXPR_RUNNING;
    fprintf(stderr, "[sup] cp_exec → SUP_EXPR_RUNNING\n");
    return 0;
    #else
    return 1;
    #endif
}

/* ------------------------------------------------------------------ */
/* cp_complete — tab completion for REPL (called by JS on Tab)         */
/*                                                                     */
/* Takes the current input line, returns completion info via a shared   */
/* buffer.  JS reads the buffer after calling.                         */
/*                                                                     */
/* Returns the number of characters of common completion prefix, or    */
/* 0 if no completion.  The completions themselves are printed to       */
/* stdout (same as real CircuitPython REPL behavior).                  */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_complete")))
int cp_complete(int len) {
    if (len <= 0 || len >= INPUT_BUF_SIZE) {
        return 0;
    }
    _input_buf[len] = '\0';
    const char *compl_str;
    size_t compl_len = mp_repl_autocomplete(_input_buf, (size_t)len,
        &mp_plat_print, &compl_str);
    (void)compl_str;  /* completions are printed to stdout */
    return (int)compl_len;
}

/* ------------------------------------------------------------------ */
/* cp_ctrl_c — interrupt running expression (called by JS on Ctrl-C)   */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_ctrl_c")))
void cp_ctrl_c(void) {
    if (_state == SUP_EXPR_RUNNING || _state == SUP_CODE_RUNNING) {
        mp_sched_keyboard_interrupt();
    }
}

/* ------------------------------------------------------------------ */
/* cp_ctrl_d — soft reboot (called by JS on Ctrl-D)                    */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_ctrl_d")))
void cp_ctrl_d(void) {
    #if MICROPY_VM_YIELD_ENABLED
    if (_state == SUP_EXPR_RUNNING) {
        vm_yield_stop();
    }
    /* Try code.py, then fall back to REPL */
    if (vm_yield_start_file("/code.py") == 0) {
        _state = SUP_CODE_RUNNING;
        fprintf(stderr, "[sup] Ctrl-D → code.py\n");
    } else {
        _state = SUP_REPL;
        fprintf(stderr, "[sup] Ctrl-D → REPL\n");
    }
    #endif
}

/* ------------------------------------------------------------------ */
/* cp_continue — check if input needs more lines (called by JS)        */
/*                                                                     */
/* JS calls this after each Enter to decide whether to send the input  */
/* to cp_exec() or wait for more lines (compound statements).          */
/* Returns 1 if more input is needed, 0 if the expression is complete. */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_continue")))
int cp_continue(int len) {
    if (len <= 0 || len >= INPUT_BUF_SIZE) {
        return 0;
    }
    _input_buf[len] = '\0';
    return mp_repl_continue_with_input(_input_buf) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Semihosting event handler                                           */
/*                                                                     */
/* Called by sh_drain_events() (in hal_step, phase 1) for each event   */
/* JS appended to /sys/events.  Routes events to the appropriate       */
/* subsystem — keyboard input to _rx_buf, timers to scheduler, etc.    */
/*                                                                     */
/* This is the single entry point for all JS → Python input.  There    */
/* is no cp_push_key() export — keyboard input arrives here.           */
/* ------------------------------------------------------------------ */

void sh_on_event(const sh_event_t *evt) {
    switch (evt->event_type) {
    case SH_EVT_KEY_DOWN: {
        uint8_t c = (uint8_t)evt->event_data;
        if (c == 3) {
            /* Ctrl-C: schedule interrupt immediately rather than
             * waiting for background_tasks to scan the buffer. */
            mp_sched_keyboard_interrupt();
        }
        if (_rx_head < RX_BUF_SIZE) {
            _rx_buf[_rx_head++] = c;
        }
        break;
    }
    default:
        break;
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
/* Superseded by /sys/state (readable without WASM calls)            */
/* but kept for CLI/testing use.                                       */
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
