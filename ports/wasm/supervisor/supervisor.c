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
#include "py/mpprint.h"
#include "genhdr/mpversion.h"
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
#include "shared-module/displayio/__init__.h"
#endif

#include "supervisor/hal.h"
#include "supervisor/compile.h"
#include "supervisor/context.h"
#include "supervisor/semihosting.h"
#if CIRCUITPY_MICROCONTROLLER
#include "shared-bindings/microcontroller/Pin.h"
#endif
#if CIRCUITPY_BOARD_I2C || CIRCUITPY_BOARD_SPI || CIRCUITPY_BOARD_UART
#include "shared-module/board/__init__.h"
#endif
#if CIRCUITPY_DISPLAYIO
#include "wasm_framebuffer.h"
#endif
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
extern void vm_yield_start(mp_code_state_t *cs);
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

/* Supervisor states — exposed to JS via sh_export_state.
 * These map to context statuses but provide the legacy numbering
 * that test_browser.html expects in the status bar. */
#define SUP_UNINITIALIZED    0
#define SUP_REPL             1
#define SUP_EXPR_RUNNING     2
#define SUP_CODE_RUNNING     3  /* ctx0 is running a file (boot.py, code.py, or any) */
#define SUP_CODE_FINISHED    4
/* 5 (formerly SUP_WAITING_FOR_KEY) intentionally skipped — UX lives in JS.
 * 6 (formerly SUP_BOOT_RUNNING) intentionally skipped — C no longer
 *   distinguishes boot.py from code.py; JS knows which file it dispatched. */

static int _state = SUP_UNINITIALIZED;
static bool _ctx0_is_code = false;  /* true if ctx0 is running code.py (vs REPL expr) */
static bool _code_header_printed = false;  /* deferred so JS can inject "last edited" */
static uint32_t _frame_count = 0;

/* Forward declarations for primitives used by cp_ctrl_c, etc. */
int cp_is_runnable(void);

/* JS time source — written by cp_step(), read by mp_hal_ticks_ms().
 * Eliminates clock_gettime syscall from the hot loop. */
volatile uint64_t wasm_js_now_ms = 0;

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
            serial_write("running");
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

/* Frame budget: how many ms the supervisor gets per cp_step() call. */
#ifndef WASM_FRAME_BUDGET_MS
#define WASM_FRAME_BUDGET_MS 13
#endif

/* ------------------------------------------------------------------ */
/* Supervisor debug logging                                            */
/*                                                                     */
/* In browser mode, debug output goes to stderr (→ console.log via JS).*/
/* Gated by a runtime flag so settings.toml or boot.py can silence it. */
/* ------------------------------------------------------------------ */

static bool _debug_enabled = true;  /* default on; JS or boot.py can turn off */

#define SUP_DEBUG(fmt, ...) do { \
    if (_debug_enabled) fprintf(stderr, "[sup] " fmt "\n", ##__VA_ARGS__); \
} while (0)

/* ------------------------------------------------------------------ */
/* Lifecycle messages — printed to stdout (serial/displayio terminal)   */
/*                                                                     */
/* These match real CircuitPython board messages where applicable.      */
/* ------------------------------------------------------------------ */

static void _print_banner(void) {
    mp_printf(&mp_plat_print, "%s running on wasm-browser\n",
              MICROPY_BANNER_NAME_AND_VERSION);
}

static void _print_soft_reboot(void) {
    mp_hal_stdout_tx_str("\r\nsoft reboot\r\n");
}

/* _print_code_done, _print_press_any_key, _print_auto_reload_status,
 * _print_code_py_header removed — JS owns all these UX strings now
 * (emitted by runBoardLifecycle() and the wait-for-key handler). */

/* Lifecycle helpers (_boot_to_code / _restart_lifecycle / _start_boot_py /
 * _start_code_py) removed in Stage 3.  JS orchestrates the boot.py →
 * code.py → REPL sequence via runBoardLifecycle(); C provides primitives
 * (cp_run, cp_banner, cp_soft_reboot) but no longer owns the sequence. */

/* ------------------------------------------------------------------ */
/* Static buffers                                                      */
/* ------------------------------------------------------------------ */

static char heap[WASM_GC_HEAP_SIZE];

/* Shared input buffer — JS writes source text here before calling
 * cp_exec() or cp_continue().  Exported via cp_input_buf_addr(). */
#define INPUT_BUF_SIZE 4096
static char _input_buf[INPUT_BUF_SIZE];

/* ------------------------------------------------------------------ */
/* Background tasks — called from MICROPY_VM_HOOK_LOOP (vm_yield.c)    */
/*                                                                     */
/* "Things Python needs the platform to do to keep going."             */
/* ------------------------------------------------------------------ */

/* serial.c owns the rx buffer and provides these */
extern void serial_push_byte(uint8_t c);
extern void serial_check_interrupt(void);

void wasm_background_tasks(void) {
    serial_check_interrupt();

    /* Future: check MEMFS hw endpoints, cursor blink, display dirty */
}

/* ------------------------------------------------------------------ */
/* Core init — called once, sets up the VM                             */
/* ------------------------------------------------------------------ */

static void _core_init(void) {
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(heap, heap + WASM_GC_HEAP_SIZE);

    /* Open /hal/ fd endpoints before mp_init() — hardware must be
     * available before Python starts. */
    hal_init();
    sh_init();

    mp_init();

    /* Initialize jsffi proxy tables (must come after mp_init for GC). */
    #if MICROPY_PY_JSFFI
    {
        extern void proxy_c_init(void);
        proxy_c_init();
    }
    #endif

    /* Initialize the context system — sets up pystack for context 0. */
    cp_context_init();

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
        /* MICROPY_PY_SYS_PATH_ARGV_DEFAULTS is 0, so mp_init() doesn't
         * create the list.  We must allocate it before appending. */
        mp_sys_path = mp_obj_new_list(0, NULL);
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

    SUP_DEBUG("CircuitPython WASM (heap=%dK)", WASM_GC_HEAP_SIZE / 1024);

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

    /* cp_init no longer runs boot.py / code.py — JS orchestrates the
     * lifecycle via runBoardLifecycle().  We come up in REPL state with
     * ctx0 idle; JS calls cp_banner() and cp_run() as needed. */
    _state = SUP_REPL;
    _ctx0_is_code = false;
    _code_header_printed = false;
    cp_context_set_status(0, CTX_IDLE);

    SUP_DEBUG("cp_init complete (heap=%dK budget=%dms) — awaiting JS orchestration",
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
int cp_step(uint32_t now_ms) {
    if (_state == SUP_UNINITIALIZED) {
        cp_init();
    }

    wasm_js_now_ms = (uint64_t)now_ms;
    _frame_count++;

    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_set_frame_start(wasm_js_now_ms);
    #endif

    /* ── Phase 1: HAL step ── */
    hal_step();

    /* ── Phase 2: Tick catchup + background callbacks ── */
    /* background_callback_run_all() calls port_background_task() first
     * (which simulates elapsed ms via supervisor_tick), then drains
     * the callback queue (supervisor_background_tick, etc). */
    RUN_BACKGROUND_TASKS;

    /* ── Phase 3: Python VM ── */
    #if MICROPY_VM_YIELD_ENABLED
    if (wasm_cli_mode) {
        /* CLI mode: feed queued keystrokes via pyexec. */
        #if MICROPY_REPL_EVENT_DRIVEN
        while (serial_bytes_available() > 0) {
            char c = serial_read();
            int ret = pyexec_event_repl_process_char(c);
            if (ret & PYEXEC_FORCED_EXIT) {
                pyexec_event_repl_init();
            }
        }
        #endif
    } else {
        /* Browser mode: scheduler-driven context dispatch.
         * Pick the highest-priority runnable context and step it. */
        int ctx_id = cp_scheduler_pick(wasm_js_now_ms);

        if (ctx_id >= 0) {
            int prev_id = cp_context_active();
            if (ctx_id != prev_id) {
                cp_context_save(prev_id);
                cp_context_restore(ctx_id);
            }

            cp_context_set_status(ctx_id, CTX_RUNNING);
            int ret = vm_yield_step();

            if (ret == 0 || ret == 2) {
                /* Normal completion or exception — context is done */
                cp_context_set_status(ctx_id, CTX_DONE);
                SUP_DEBUG("ctx%d done", ctx_id);
                /* JS's runBoardLifecycle awaits ctx0 idle to advance stages;
                 * no edge-trigger latch needed here. */
            } else {
                /* Yielded — sleeping or budget exhausted.
                 * mp_hal_delay_ms already set CTX_SLEEPING if applicable;
                 * otherwise mark as yielded for next frame. */
                if (cp_context_get_status(ctx_id) != CTX_SLEEPING) {
                    cp_context_set_status(ctx_id, CTX_YIELDED);
                }
            }
        }

        /* Update _state from context 0.  JS-triggered transitions
         * (cp_run, cp_ctrl_d) set the initial _state; this block handles
         * ongoing transitions (e.g. expression finishes → CTX_DONE → SUP_REPL). */
        uint8_t ctx0 = cp_context_get_status(0);
        if (ctx0 == CTX_IDLE || ctx0 == CTX_DONE || ctx0 == CTX_FREE) {
            /* Whatever ctx0 was running, it's done.  Default to REPL;
             * no "waiting for key" substate — JS owns that UX now. */
            _state = SUP_REPL;
            _ctx0_is_code = false;
        } else if (ctx0 >= CTX_RUNNABLE && ctx0 <= CTX_SLEEPING) {
            _state = _ctx0_is_code ? SUP_CODE_RUNNING : SUP_EXPR_RUNNING;
        }
    }
    #endif

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
    #if CIRCUITPY_DISPLAYIO
    wasm_cursor_info_update();
    #endif

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
/* cp_frozen_names — expose frozen module list to JS                    */
/*                                                                     */
/* mp_frozen_names is a null-separated string array with a double-null */
/* terminator.  JS can parse it directly from linear memory.           */
/* ------------------------------------------------------------------ */

extern const char mp_frozen_names[];

__attribute__((export_name("cp_frozen_names_addr")))
uintptr_t cp_frozen_names_addr(void) {
    return (uintptr_t)mp_frozen_names;
}

/* ------------------------------------------------------------------ */
/* cp_run — unified run-entry (called by JS)                            */
/*                                                                     */
/* One function to start any Python work.  JS writes source text (for  */
/* CP_SRC_EXPR) or a path (for CP_SRC_FILE) into the shared input      */
/* buffer, then calls cp_run(src_kind, src_len, ctx, priority).        */
/*                                                                     */
/*   src_kind:                                                         */
/*     CP_SRC_EXPR (0) — compile buffer as REPL expression/statement   */
/*                       (MP_PARSE_SINGLE_INPUT)                       */
/*     CP_SRC_FILE (1) — buffer contains a file path; compile the file */
/*                       (MP_PARSE_FILE_INPUT)                         */
/*                                                                     */
/*   ctx:                                                              */
/*     CP_CTX_MAIN (-1) — run on context 0 (fails if ctx0 is busy)     */
/*     CP_CTX_NEW  (-2) — allocate a new context                       */
/*                                                                     */
/*   priority — only used when ctx=CP_CTX_NEW                          */
/*                                                                     */
/* Returns:                                                            */
/*     ≥ 0 — context id the code is running in (0 for MAIN, 1+ for NEW) */
/*     -1  — ctx0 busy (only when ctx=CP_CTX_MAIN)                     */
/*     -2  — compile error                                             */
/*     -3  — no free context slot (only when ctx=CP_CTX_NEW)           */
/*     -4  — VM yield disabled / invalid args                          */
/* ------------------------------------------------------------------ */

#define CP_SRC_EXPR 0
#define CP_SRC_FILE 1
#define CP_CTX_MAIN (-1)
#define CP_CTX_NEW  (-2)

__attribute__((export_name("cp_run")))
int cp_run(int src_kind, int src_len, int ctx, int priority) {
    #if MICROPY_VM_YIELD_ENABLED
    if (src_len <= 0 || src_len >= INPUT_BUF_SIZE) {
        return -4;
    }
    if (src_kind != CP_SRC_EXPR && src_kind != CP_SRC_FILE) {
        return -4;
    }
    _input_buf[src_len] = '\0';

    /* MAIN target: ctx0 must be idle.  REPL is the only legal entry state
     * for user-initiated runs on ctx0 (boot.py / code.py are driven by
     * the internal lifecycle path). */
    if (ctx == CP_CTX_MAIN && _state != SUP_REPL) {
        return -1;
    }

    /* Allocate target context */
    int target_id;
    int prev_id = cp_context_active();
    if (ctx == CP_CTX_MAIN) {
        target_id = 0;
        if (prev_id != 0) {
            cp_context_save(prev_id);
            cp_context_restore(0);
        }
    } else if (ctx == CP_CTX_NEW) {
        target_id = cp_context_create((uint8_t)priority);
        if (target_id < 0) {
            return -3;
        }
        /* Preserve caller's globals/locals so compilation of this new
         * context sees the importable module dict. */
        mp_obj_dict_t *caller_globals = mp_globals_get();
        mp_obj_dict_t *caller_locals = mp_locals_get();
        cp_context_save(prev_id);
        cp_context_restore(target_id);
        mp_globals_set(caller_globals);
        mp_locals_set(caller_locals);
    } else {
        return -4;
    }

    /* Compile on the target context's pystack */
    mp_code_state_t *cs;
    if (src_kind == CP_SRC_EXPR) {
        cs = cp_compile_str(_input_buf, (size_t)src_len, MP_PARSE_SINGLE_INPUT);
    } else {
        cs = cp_compile_file(_input_buf);
    }
    if (cs == NULL) {
        /* Compile failed — restore caller's context; destroy NEW target */
        if (ctx == CP_CTX_NEW) {
            cp_context_restore(prev_id);
            cp_context_destroy(target_id);
        }
        return -2;
    }

    /* Start the VM, mark runnable, hand off to the scheduler */
    vm_yield_start(cs);
    cp_context_load(target_id, cs);
    cp_context_set_status(target_id, CTX_RUNNABLE);

    if (ctx == CP_CTX_MAIN) {
        /* Convention: EXPR = REPL expression (ctx0_is_code=false),
         *             FILE = code.py-like run (ctx0_is_code=true).
         * JS controls which by choosing src_kind. */
        _ctx0_is_code = (src_kind == CP_SRC_FILE);
        _state = _ctx0_is_code ? SUP_CODE_RUNNING : SUP_EXPR_RUNNING;
        SUP_DEBUG("cp_run → %s on ctx0",
                  _ctx0_is_code ? "SUP_CODE_RUNNING" : "SUP_EXPR_RUNNING");
    } else {
        /* Save the new context's state and switch back to caller */
        cp_context_save(target_id);
        cp_context_restore(prev_id);
        SUP_DEBUG("cp_run → ctx%d (kind=%d pri=%d)",
                  target_id, src_kind, priority);
    }
    return target_id;
    #else
    (void)src_kind; (void)src_len; (void)ctx; (void)priority;
    return -4;
    #endif
}

/* ------------------------------------------------------------------ */
/* Legacy run-entry shims — thin wrappers around cp_run.                */
/* Kept as exports for JS callers that haven't migrated yet; scheduled  */
/* for removal once all consumers use cp_run directly.                  */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_exec")))
int cp_exec(int len) {
    /* Legacy return codes: 0=ok, 1=compile error, 2=busy.
     * cp_run returns: 0=ok (ctx0), -1=busy, -2=compile error, -4=bad args. */
    int r = cp_run(CP_SRC_EXPR, len, CP_CTX_MAIN, 0);
    if (r == 0) return 0;
    if (r == -1) return 2;
    return 1;
}

__attribute__((export_name("cp_context_exec")))
int cp_context_exec(int len, int priority) {
    /* Legacy: ≥1 ctx id, -1 no slots, -2 compile error.
     * cp_run:  ≥1 ctx id, -3 no slots, -2 compile error. */
    int r = cp_run(CP_SRC_EXPR, len, CP_CTX_NEW, priority);
    if (r >= 0) return r;
    if (r == -3) return -1;
    return -2;
}

__attribute__((export_name("cp_context_exec_file")))
int cp_context_exec_file(int path_len, int priority) {
    int r = cp_run(CP_SRC_FILE, path_len, CP_CTX_NEW, priority);
    if (r >= 0) return r;
    if (r == -3) return -1;
    return -2;
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
/* cp_syntax_check — parse + compile without running                    */
/*                                                                     */
/* Pure function (no side effects on active context): parses the shared */
/* input buffer as a module body, compiles it, and returns whether the */
/* source is syntactically valid.  Any exception (SyntaxError, etc.)   */
/* is printed to stderr via mp_obj_print_exception — JS can capture    */
/* stderr for structured details.                                      */
/*                                                                     */
/* Safe to call mid-run: uses a scoped nlr_buf, doesn't touch any      */
/* context's code_state or globals.  The compiled module_fun is not    */
/* retained after the call returns.                                    */
/*                                                                     */
/* Returns:                                                            */
/*   0 — source compiles cleanly                                       */
/*   1 — parse/compile error (details already written to stderr)       */
/*   2 — input length out of range                                     */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_syntax_check")))
int cp_syntax_check(int len) {
    if (len <= 0 || len >= INPUT_BUF_SIZE) {
        return 2;
    }
    _input_buf[len] = '\0';

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(
            MP_QSTR__lt_stdin_gt_, _input_buf, (size_t)len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        /* mp_compile allocates but the result is only rooted through the
         * active function; since we discard it here, the GC will reclaim
         * it on the next sweep.  This is fine — we're just validating. */
        (void)mp_compile(&parse_tree, source_name, false);
        nlr_pop();
        return 0;
    } else {
        mp_obj_print_exception(&mp_stderr_print,
            MP_OBJ_FROM_PTR(nlr.ret_val));
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* cp_ctrl_c — interrupt running expression (called by JS on Ctrl-C)   */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_ctrl_c")))
void cp_ctrl_c(void) {
    if (cp_is_runnable()) {
        mp_sched_keyboard_interrupt();
    }
}

/* ------------------------------------------------------------------ */
/* cp_banner — print the CircuitPython banner (version + board id)     */
/*                                                                     */
/* JS calls this at lifecycle start.  C owns the banner string because */
/* the version/board identifiers live in MICROPY_BANNER_NAME_AND_VERSION */
/* and the port-specific "running on <board>" line.                    */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_banner")))
void cp_banner(void) {
    _print_banner();
}

/* ------------------------------------------------------------------ */
/* cp_cleanup — tear down Layer 3 (user session), keep Layer 2 (VM).   */
/*                                                                     */
/* Layer 3 = claimed pins, bus singletons, display root_group, running */
/* bytecode, GC-managed Python objects from the user's code.           */
/*                                                                     */
/* Layer 2 = mp runtime, GC heap (structure), pystack pool, HAL fds,   */
/* display framebuffer, supervisor terminal.  These survive cleanup.   */
/*                                                                     */
/* Called by JS at every mode transition (Run, REPL, Stop).            */
/* Idempotent — safe to call when already clean.                       */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_cleanup")))
void cp_cleanup(void) {
    #if MICROPY_VM_YIELD_ENABLED

    /* 1. Stop any running bytecode (frees pystack frame for ctx0). */
    vm_yield_stop();
    cp_context_set_status(0, CTX_IDLE);

    /* 2. Reset hardware in dependency order.
     *    Displays first (may inline heap-allocated bus objects).
     *    Board buses next (releases pins from never_reset).
     *    Pins last (clears .claimed flags). */
    #if CIRCUITPY_DISPLAYIO
    reset_displays();
    #endif
    #if CIRCUITPY_BOARD_I2C || CIRCUITPY_BOARD_SPI || CIRCUITPY_BOARD_UART
    reset_board_buses();
    #endif
    #if CIRCUITPY_MICROCONTROLLER
    reset_all_pins();
    #endif

    /* 3. Collect garbage — frees unreachable heap objects from previous
     *    session while preserving Layer 2 state (VFS mounts, display
     *    objects, etc.) which remain reachable from MP_STATE_VM roots.
     *    gc_collect() (not gc_sweep_all!) does a mark+sweep that only
     *    frees objects no longer referenced. */
    gc_collect();

    /* 4. Reset supervisor state machine. */
    _ctx0_is_code = false;
    _code_header_printed = false;
    _state = SUP_REPL;

    SUP_DEBUG("cleanup — Layer 3 torn down, Layer 2 intact");
    #endif
}

/* ------------------------------------------------------------------ */
/* cp_soft_reboot — backward compat wrapper around cp_cleanup.         */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_soft_reboot")))
void cp_soft_reboot(void) {
    _print_soft_reboot();
    cp_cleanup();
}

/* ------------------------------------------------------------------ */
/* cp_ctrl_d — Ctrl-D from JS: soft reboot (JS re-invokes lifecycle)   */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_ctrl_d")))
void cp_ctrl_d(void) {
    cp_soft_reboot();
}

/* cp_auto_reload removed — JS handles file-change events directly by
 * calling cp_soft_reboot() then runBoardLifecycle(). */

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
/* Called by sh_drain_event_ring() (in hal_step, phase 1) for each     */
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
        serial_push_byte(c);
        break;
    }
    default:
        break;
    }
}

/* cp_run_file removed — zero consumers; superseded by
 * cp_run(CP_SRC_FILE, path_len, CP_CTX_MAIN, 0). */

/* ------------------------------------------------------------------ */
/* Exported: accessors                                                 */
/* Superseded by /sys/state (readable without WASM calls)            */
/* but kept for CLI/testing use.                                       */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_get_state")))
int cp_get_state(void) {
    return (int)_state;
}

/* ------------------------------------------------------------------ */
/* cp_is_runnable / cp_needs_input                                      */
/*                                                                     */
/* Minimal JS-facing predicates that replace SUP_* polling.            */
/* JS shouldn't need to know which supervisor substate we're in — it   */
/* only needs: is there still work for me to tick, and does anything   */
/* want input?                                                         */
/*                                                                     */
/* (The cp_ctx0_completed_code edge-trigger from Stage 1 was removed in */
/* Stage 3; runBoardLifecycle awaits ctx0 idle directly and owns the   */
/* "Code done running. / Press any key ..." UX itself.)                */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_is_runnable")))
int cp_is_runnable(void) {
    /* Any context actively doing work? */
    for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
        uint8_t s = cp_context_get_status(i);
        if (s == CTX_RUNNABLE || s == CTX_RUNNING
            || s == CTX_YIELDED || s == CTX_SLEEPING) {
            return 1;
        }
    }
    return 0;
}

__attribute__((export_name("cp_needs_input")))
int cp_needs_input(void) {
    /* True when REPL is idle and ready to accept a new expression.
     * No context runnable AND ctx0 is idle/done => waiting for JS input. */
    if (cp_is_runnable()) return 0;
    uint8_t s0 = cp_context_get_status(0);
    return (s0 == CTX_IDLE || s0 == CTX_DONE || s0 == CTX_FREE) ? 1 : 0;
}

#if 0  /* removed in Stage 3 — see comment block above */
__attribute__((export_name("cp_ctx0_completed_code")))
int cp_ctx0_completed_code(void) {
    return 0;
}
#endif

__attribute__((export_name("cp_set_debug")))
void cp_set_debug(int enabled) {
    _debug_enabled = (enabled != 0);
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
