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
#include "supervisor/port_memory.h"
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
extern void vm_yield_set_frame_start(void);
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

#include "supervisor/supervisor_internal.h"

/* All supervisor state lives in port_mem (port_memory.c).
 * cp_is_runnable, cp_exec, cp_cleanup, cp_wake declared in supervisor_internal.h. */

/* ------------------------------------------------------------------ */
/* supervisor_execution_status — called by status_bar.c                */
/* ------------------------------------------------------------------ */

#if CIRCUITPY_STATUS_BAR
#include "supervisor/shared/serial.h"

void supervisor_execution_status(void) {
    switch (sup_state) {
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

/* Configuration and state accessors come from port_memory.h
 * (included via supervisor_internal.h).  SUP_DEBUG macro and all
 * state names (sup_state, wasm_js_now_ms, etc.) are #defined there
 * to reference port_mem fields. */

/* ------------------------------------------------------------------ */
/* Lifecycle messages — printed to stdout (serial/displayio terminal)   */
/*                                                                     */
/* These match real CircuitPython board messages where applicable.      */
/* ------------------------------------------------------------------ */

void sup_print_banner(void) {
    mp_printf(&mp_plat_print, "%s running on wasm-browser\n",
              MICROPY_BANNER_NAME_AND_VERSION);
}

void sup_print_soft_reboot(void) {
    mp_hal_stdout_tx_str("\r\nsoft reboot\r\n");
}

/* _print_code_done, _print_press_any_key, _print_auto_reload_status,
 * _print_code_py_header removed — JS owns all these UX strings now
 * (emitted by runBoardLifecycle() and the wait-for-key handler). */

/* Lifecycle helpers (_boot_to_code / _restart_lifecycle / _start_boot_py /
 * _start_code_py) removed in Stage 3.  JS orchestrates the boot.py →
 * code.py → REPL sequence via runBoardLifecycle(); C provides primitives
 * (cp_run, cp_banner, cp_soft_reboot) but no longer owns the sequence. */

/* Static buffers (GC heap, input buffer) now live in port_mem. */

/* serial.c owns the rx buffer and provides these */
extern void serial_push_byte(uint8_t c);
extern void serial_check_interrupt(void);

/* ------------------------------------------------------------------ */
/* Core init — called once, sets up the VM                             */
/* ------------------------------------------------------------------ */

static void _core_init(void) {
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(port_gc_heap(), port_gc_heap() + port_gc_heap_size());

    /* Open /hal/ fd endpoints before mp_init() — hardware must be
     * available before Python starts. */
    hal_init();
    sh_init();

    mp_init();

    /* Populate pin_meta categories from the board dict.
     * Must come after mp_init() since the dict uses qstrs. */
    hal_init_pin_categories();

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

}

/* ------------------------------------------------------------------ */
/* cp_hw_init — Layer 2b: Virtual Hardware initialization              */
/*                                                                     */
/* Sets up the display framebuffer + supervisor terminal.  Must be     */
/* called after _core_init / cp_init (needs mp_init + GC for alloc).  */
/* Separate from cp_init so JS can control when hardware comes up.     */
/* ------------------------------------------------------------------ */

static bool _hw_initialized = false;

__attribute__((export_name("cp_hw_init")))
void cp_hw_init(void) {
    if (_hw_initialized) return;
    _hw_initialized = true;

    #if CIRCUITPY_DISPLAYIO
    board_display_init();
    #endif

    #if CIRCUITPY_STATUS_BAR
    supervisor_status_bar_init();
    supervisor_status_bar_start();
    #endif

    SUP_DEBUG("cp_hw_init complete (display + terminal ready)");
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
    cp_hw_init();

    /* Boot sequence: run boot.py if present. */
    {
        pyexec_result_t result;
        pyexec_file_if_exists("/boot.py", &result);
    }

    sup_state = SUP_REPL;

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
    if (sup_state != SUP_UNINITIALIZED) return 0;

    _core_init();    /* Layer 2a: VM (mp_init, GC, pystack, HAL fds) */
    cp_hw_init();    /* Layer 2b: VH (display, terminal) */

    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_set_budget(WASM_FRAME_BUDGET_MS);
    #endif

    sup_state = SUP_REPL;
    sup_ctx0_is_code = false;
    sup_code_header_printed = false;
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

/* ------------------------------------------------------------------ */
/* cp_hw_step — Port layer: JS↔C sync + background task drain.         */
/*                                                                     */
/* Must be called EVERY frame regardless of VM state.                  */
/* In READY: refreshes supervisor terminal + cursor.                   */
/* In EXECUTING: refreshes user's displayio output.                    */
/* In SUSPENDED: keeps display alive while VM sleeps.                  */
/*                                                                     */
/* This is the once-per-frame entry point for all port-level work:     */
/*   hal_step()            — drain JS events, latch input pins         */
/*   RUN_BACKGROUND_TASKS  — port_background_task (~1ms tick gate)     */
/*                           + drain callback queue (displayio, etc.)  */
/*   hal_export_dirty()    — flush HAL state back to MEMFS             */
/*                                                                     */
/* The VM also drains callbacks via MICROPY_VM_HOOK_RETURN (on every   */
/* function return).  Between those drains, the only per-branch work   */
/* is serial_check_interrupt() + budget check (wasm_vm_hook_loop).     */
/*                                                                     */
/* If mid-frame hardware polling is needed (e.g., I2C device reads     */
/* during long-running Python loops), either:                          */
/*   (a) add the work to port_background_task() — it already runs at  */
/*       every callback drain (function returns + here), or            */
/*   (b) promote MICROPY_VM_HOOK_LOOP to also call RUN_BACKGROUND_TASKS*/
/*       for per-branch draining.  The time-gated port_background_task */
/*       keeps the cost bounded either way.                            */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_hw_step")))
void cp_hw_step(uint32_t now_ms) {
    (void)now_ms;  /* Time read fresh via clock_gettime when needed */
    sup_frame_count++;

    /* HAL step — drive simulated hardware (poll /hal/ endpoints) */
    hal_step();

    /* Tick catchup + background callbacks */
    RUN_BACKGROUND_TASKS;

    /* Background work drained — clear the pending flag.
     * port_wake_main_task() sets this when new callbacks are registered;
     * JS reads it to know whether to schedule cp_hw_step() while idle. */
    sh_clear_bg_pending();

    /* HAL export — flush hw_state changes to /hal/ endpoints */
    hal_export_dirty();

    /* State export for JS-side debugging */
    {
        uint32_t yr = 0, ya = 0, depth = 0;
        #if MICROPY_VM_YIELD_ENABLED
        yr = (uint32_t)mp_vm_yield_reason;
        ya = (uint32_t)mp_vm_yield_arg;
        #endif
        #if MICROPY_ENABLE_PYSTACK
        depth = (uint32_t)mp_pystack_usage();
        #endif
        sh_export_state((uint32_t)sup_state, yr, ya, sup_frame_count, depth);
    }

    /* Update cursor info for JS-side rendering */
    #if CIRCUITPY_DISPLAYIO
    wasm_cursor_info_update();
    #endif
}

/* ------------------------------------------------------------------ */
/* cp_step — VM work: bytecode stepping within frame budget.           */
/*                                                                     */
/* Only meaningful when cp_state() == EXECUTING.                       */
/* No-op when READY or SUSPENDED.                                      */
/* Call cp_hw_step() separately (before or after) for display/HW.      */
/*                                                                     */
/* For backward compat, cp_step also calls cp_hw_step internally so    */
/* existing JS that calls only cp_step still works.                    */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_step")))
int cp_step(uint32_t now_ms) {
    if (sup_state == SUP_UNINITIALIZED) {
        cp_init();
    }

    /* VH work — always runs */
    cp_hw_step(now_ms);

    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_set_frame_start();

    /* VM work — only when there's code to execute */
    if (wasm_cli_mode) {
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
        int ctx_id = cp_scheduler_pick(mp_hal_ticks_ms());

        if (ctx_id >= 0) {
            int prev_id = cp_context_active();
            if (ctx_id != prev_id) {
                cp_context_save(prev_id);
                cp_context_restore(ctx_id);
            }

            cp_context_set_status(ctx_id, CTX_RUNNING);
            int ret = vm_yield_step();

            if (ret == 0 || ret == 2) {
                cp_context_set_status(ctx_id, CTX_DONE);
                SUP_DEBUG("ctx%d done", ctx_id);
            } else {
                if (cp_context_get_status(ctx_id) != CTX_SLEEPING) {
                    cp_context_set_status(ctx_id, CTX_YIELDED);
                }
            }
        }

        /* Update legacy sup_state from context 0. */
        uint8_t ctx0 = cp_context_get_status(0);
        if (ctx0 == CTX_IDLE || ctx0 == CTX_DONE || ctx0 == CTX_FREE) {
            sup_state = SUP_REPL;
            sup_ctx0_is_code = false;
        } else if (ctx0 >= CTX_RUNNABLE && ctx0 <= CTX_SLEEPING) {
            sup_state = sup_ctx0_is_code ? SUP_CODE_RUNNING : SUP_EXPR_RUNNING;
        }
    }
    #endif

    return (int)sup_state;
}

/* ------------------------------------------------------------------ */
/* wasm_frame — One frame of the WASM process.                         */
/*                                                                     */
/* The single entry point for JS. Internally does:                     */
/*   1. Port check  — drain events, update VH, run background callbacks */
/*   2. Supervisor   — pick context, check deadlines                    */
/*   3. VM burst     — execute bytecode within remaining budget         */
/*   4. Export       — write state, trace info to linear memory         */
/*                                                                     */
/* Returns a packed uint32_t with per-layer results:                   */
/*   bits  0-7:  port result   (WASM_PORT_*)                           */
/*   bits  8-15: supervisor    (WASM_SUP_*)                             */
/*   bits 16-23: VM result     (WASM_VM_*)                              */
/*                                                                     */
/* JS unpacks to make nuanced scheduling decisions combining C results */
/* with JS-local state (tab visibility, user settings, display).       */
/* ------------------------------------------------------------------ */

/* Port layer */
#define WASM_PORT_QUIET         0  /* no events, no bg work */
#define WASM_PORT_EVENTS        1  /* drained events from ring */
#define WASM_PORT_BG_PENDING    2  /* background work still pending */
#define WASM_PORT_HW_CHANGED    3  /* hardware state changed */

/* Supervisor layer */
#define WASM_SUP_IDLE           0  /* no contexts to run */
#define WASM_SUP_SCHEDULED      1  /* picked and ran a context */
#define WASM_SUP_CTX_DONE       2  /* a context completed this frame */
#define WASM_SUP_ALL_SLEEPING   3  /* all contexts sleeping */

/* VM layer */
#define WASM_VM_NOT_RUN         0  /* supervisor didn't run VM */
#define WASM_VM_YIELDED         1  /* budget expired, more to do */
#define WASM_VM_SLEEPING        2  /* time.sleep, waiting for deadline */
#define WASM_VM_COMPLETED       3  /* code finished normally */
#define WASM_VM_EXCEPTION       4  /* unhandled exception */
#define WASM_VM_SUSPENDED       5  /* waiting for I/O / external event */

#define WASM_FRAME_RESULT(port, sup, vm) \
    ((port) | ((sup) << 8) | ((vm) << 16))

extern bool background_callback_pending(void);
extern uint32_t cp_next_wake_ms(uint32_t now_ms);

/* ── WASM imports: C asks JS for scheduling ── */

/* Request the next animation frame.  C calls this at the end of
 * wasm_frame() when there's more work to do.  JS implements it as
 * requestAnimationFrame(cb) where cb calls wasm_frame() again.
 *
 * hint values:
 *   0 = ASAP (rAF or setTimeout(0))
 *   1..N = delay in ms (setTimeout(N) — for sleeping contexts)
 *   0xFFFFFFFF = don't schedule (idle — wait for external event)
 *
 * If JS doesn't provide this import, the function is a no-op and
 * JS continues to drive the loop externally (backward compat). */
__attribute__((import_module("port"), import_name("requestFrame")))
extern void port_request_frame(uint32_t hint_ms);

/* Whether C should drive the frame loop (JS provided the import). */
static bool _c_driven_loop = false;

__attribute__((export_name("cp_set_c_driven_loop")))
void cp_set_c_driven_loop(int enabled) {
    _c_driven_loop = (enabled != 0);
}

/* Determine next-frame hint from wasm_frame results. */
static uint32_t _schedule_hint(uint8_t port_result, uint8_t sup_result,
                                uint8_t vm_result) {
    /* VM yielded = more bytecode to run — ASAP */
    if (vm_result == WASM_VM_YIELDED) return 0;

    /* Background work pending — keep ticking */
    if (port_result == WASM_PORT_BG_PENDING) return 0;

    /* Context just finished — one more frame for cleanup */
    if (sup_result == WASM_SUP_CTX_DONE) return 0;

    /* VM sleeping — ask context system for nearest deadline */
    if (vm_result == WASM_VM_SLEEPING || sup_result == WASM_SUP_ALL_SLEEPING) {
        return cp_next_wake_ms(mp_hal_ticks_ms());
    }

    /* Idle — no work, no sleeping contexts */
    return 0xFFFFFFFF;  /* fully idle */
}

__attribute__((export_name("wasm_frame")))
uint32_t wasm_frame(uint32_t now_ms, uint32_t budget_ms) {
    if (sup_state == SUP_UNINITIALIZED) {
        cp_init();
    }

    uint8_t port_result = WASM_PORT_QUIET;
    uint8_t sup_result  = WASM_SUP_IDLE;
    uint8_t vm_result   = WASM_VM_NOT_RUN;

    /* ── 1. Port check ── */
    cp_hw_step(now_ms);

    /* Detect if events were processed or hardware changed */
    /* (bg_pending set by port_wake_main_task during callback drain) */
    if (background_callback_pending()) {
        port_result = WASM_PORT_BG_PENDING;
    }

    /* ── 2. Supervisor: pick context, run VM ── */
    #if MICROPY_VM_YIELD_ENABLED
    if (wasm_cli_mode) {
        /* CLI mode — event-driven REPL, no yield stepping */
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
        vm_yield_set_budget(budget_ms);
        vm_yield_set_frame_start();

        int ctx_id = cp_scheduler_pick(mp_hal_ticks_ms());

        if (ctx_id >= 0) {
            /* ── 3. VM burst ── */
            int prev_id = cp_context_active();
            if (ctx_id != prev_id) {
                cp_context_save(prev_id);
                cp_context_restore(ctx_id);
            }

            cp_context_set_status(ctx_id, CTX_RUNNING);
            sup_result = WASM_SUP_SCHEDULED;

            int ret = vm_yield_step();

            if (ret == 0) {
                /* Normal completion */
                cp_context_set_status(ctx_id, CTX_DONE);
                SUP_DEBUG("ctx%d done", ctx_id);
                sup_result = WASM_SUP_CTX_DONE;
                vm_result = WASM_VM_COMPLETED;
            } else if (ret == 2) {
                /* Exception */
                cp_context_set_status(ctx_id, CTX_DONE);
                SUP_DEBUG("ctx%d done", ctx_id);
                sup_result = WASM_SUP_CTX_DONE;
                vm_result = WASM_VM_EXCEPTION;
            } else {
                /* Yielded — determine why */
                if (cp_context_get_status(ctx_id) == CTX_SLEEPING) {
                    vm_result = WASM_VM_SLEEPING;
                } else {
                    cp_context_set_status(ctx_id, CTX_YIELDED);
                    if (mp_vm_yield_reason == 1 /* YIELD_SLEEP */) {
                        vm_result = WASM_VM_SLEEPING;
                    } else if (mp_vm_yield_reason == 3 /* YIELD_IO_WAIT */ ||
                               mp_vm_yield_reason == 4 /* YIELD_STDIN */) {
                        vm_result = WASM_VM_SUSPENDED;
                    } else {
                        vm_result = WASM_VM_YIELDED;
                    }
                }
            }
        } else {
            /* No context picked — check why */
            bool any_sleeping = false;
            for (int i = 0; i < CP_MAX_CONTEXTS; i++) {
                uint8_t st = cp_context_get_status(i);
                if (st == CTX_SLEEPING) { any_sleeping = true; break; }
            }
            sup_result = any_sleeping ? WASM_SUP_ALL_SLEEPING : WASM_SUP_IDLE;
        }

        /* Update legacy sup_state from context 0 */
        uint8_t ctx0 = cp_context_get_status(0);
        if (ctx0 == CTX_IDLE || ctx0 == CTX_DONE || ctx0 == CTX_FREE) {
            sup_state = SUP_REPL;
            sup_ctx0_is_code = false;
        } else if (ctx0 >= CTX_RUNNABLE && ctx0 <= CTX_SLEEPING) {
            sup_state = sup_ctx0_is_code ? SUP_CODE_RUNNING : SUP_EXPR_RUNNING;
        }
    }
    #endif

    uint32_t result = WASM_FRAME_RESULT(port_result, sup_result, vm_result);
    sh_set_frame_result(result);

    /* C-driven scheduling: tell JS when to call wasm_frame() next. */
    if (_c_driven_loop) {
        uint32_t hint = _schedule_hint(port_result, sup_result, vm_result);
        port_request_frame(hint);
    }

    return result;
}

/* JS-facing exports (cp_print, cp_input_buf_addr, cp_frozen_names_addr,
 * cp_run, cp_exec, cp_ctrl_c, cp_cleanup, cp_wake, cp_state, etc.)
 * have moved to port.c — the JS-facing boundary layer. */

/* (moved to port.c) */

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
    /* Check all wake registrations for every event.
     * This is the event-driven wake mechanism: Python code registers
     * interest in specific events, and matching events wake contexts. */
    cp_wake_check_event(evt->event_type, evt->event_data);

    /* Route to specific handlers */
    switch (evt->event_type) {
    case SH_EVT_KEY_DOWN: {
        uint8_t c = (uint8_t)evt->event_data;
        if (c == 3) {
            mp_sched_keyboard_interrupt();
        }
        serial_push_byte(c);
        break;
    }
    case SH_EVT_TIMER_FIRE:
        /* Timer expired — wake the context that set it.
         * data = ctx_id (or -1 for broadcast). */
        cp_wake((int16_t)evt->event_data);
        break;
    case SH_EVT_WAKE:
        /* Generic wake — JS resolved a Promise or I/O completed.
         * data = ctx_id (or -1 for broadcast). */
        cp_wake((int16_t)evt->event_data);
        break;
    case SH_EVT_HW_CHANGE:
        /* Hardware state changed from JS — the MEMFS write already
         * happened, but we may need to wake a context waiting on
         * this pin/device.  Future: match against WFE registrations. */
        break;
    case SH_EVT_EXEC:
        /* Execute code — data=kind (0=string, 1=file), arg=len.
         * Input is already in the shared input buffer. */
        cp_exec(evt->event_data, (int)evt->arg);
        break;
    case SH_EVT_CTRL_C:
        /* Keyboard interrupt */
        if (cp_is_runnable()) {
            mp_sched_keyboard_interrupt();
        }
        break;
    case SH_EVT_CLEANUP:
        /* Layer 3 teardown */
        cp_cleanup();
        break;
    default:
        break;
    }
}

/* (wake registrations, state queries, accessors — moved to port.c) */

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
