/*
 * main_worker.c — CircuitPython WASM entry point.
 *
 * This is the board.  A single WASM binary running in a Web Worker,
 * with display, hardware peripherals, REPL, and code.py execution.
 *
 * The JS host drives the event loop by calling exported functions:
 *
 *   cp_init()              — initialize VM, display, terminal
 *   cp_push_key(c)         — enqueue keyboard input
 *   cp_step()              — process input, refresh display (REPL mode)
 *   cp_run_file("/code.py") — compile + begin stepping a .py file
 *   cp_vm_step()           — execute one burst of code.py bytecodes
 *   cp_run_repl()          — switch to event-driven REPL
 *   cp_reset()             — cleanup between code.py runs / REPL sessions
 *
 * The binary also supports _start (WASI command mode) for CLI testing
 * with wasmtime, but the primary path is the exported functions above.
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
#include "worker_terminal.h"
#include "wasm_framebuffer.h"
#include "shared-bindings/framebufferio/FramebufferDisplay.h"
#include "shared-module/displayio/__init__.h"
#endif

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#ifndef MICROPY_GC_HEAP_SIZE
#define MICROPY_GC_HEAP_SIZE (512 * 1024)
#endif

#ifndef WORKER_PYSTACK_SIZE
#define WORKER_PYSTACK_SIZE (8 * 1024)
#endif

/* ------------------------------------------------------------------ */
/* Static buffers                                                      */
/* ------------------------------------------------------------------ */

static char heap[MICROPY_GC_HEAP_SIZE];

#if MICROPY_ENABLE_PYSTACK
static mp_obj_t pystack_buf[WORKER_PYSTACK_SIZE / sizeof(mp_obj_t)];
#endif

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static bool _initialized = false;
static uint32_t _frame_count = 0;

/* Forward declarations from wasi_mphal.c */
extern uint8_t _rx_buf[256];
extern int _rx_head;
extern int _rx_tail;
extern int _rx_available(void);

/* ------------------------------------------------------------------ */
/* Step return values                                                   */
/* ------------------------------------------------------------------ */

#define CP_STEP_OK          0x00
#define CP_STEP_DISPLAY     0x01  /* framebuffer was updated */
#define CP_STEP_EXIT        0x02  /* REPL requested exit */

/* ------------------------------------------------------------------ */
/* Core init — shared by cp_init() and main()                          */
/* ------------------------------------------------------------------ */

static void _core_init(void) {
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(heap, heap + MICROPY_GC_HEAP_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    mp_pystack_init(pystack_buf,
                    pystack_buf + WORKER_PYSTACK_SIZE / sizeof(mp_obj_t));
    #endif

    mp_init();

    /* Mount VFS (WASI preopens provide the underlying fs) */
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

    #if MICROPY_PY_SYS_PATH
    if (mp_sys_path != MP_OBJ_NULL) {
        mp_obj_list_append(mp_sys_path,
                           MP_OBJ_NEW_QSTR(qstr_from_str("/lib")));
    }
    #endif

    /* Display + terminal */
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_init();
    #endif
}

/* ------------------------------------------------------------------ */
/* Exported functions — JS host API                                    */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_init")))
int cp_init(void) {
    if (_initialized) return 0;

    _core_init();

    /* Initialize event-driven REPL */
    pyexec_event_repl_init();

    /* Force initial display render */
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_refresh();
    worker_terminal_dirty = false;
    #endif

    _initialized = true;
    fprintf(stderr, "[cp] initialized (heap=%dK)\n",
            MICROPY_GC_HEAP_SIZE / 1024);
    return 0;
}

__attribute__((export_name("cp_step")))
int cp_step(void) {
    int status = CP_STEP_OK;

    /* 1. Feed any available keyboard input to REPL */
    while (_rx_available() > 0) {
        int c = _rx_buf[_rx_tail++];
        if (c == '\n') c = '\r';
        int ret = pyexec_event_repl_process_char(c);
        if (ret & PYEXEC_FORCED_EXIT) {
            status |= CP_STEP_EXIT;
        }
    }

    /* 2. Tick cursor blink + refresh display if content changed */
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_cursor_tick((uint32_t)(mp_hal_ticks_ms() & 0xFFFFFFFF));
    if (worker_terminal_dirty) {
        worker_terminal_refresh();
        worker_terminal_dirty = false;
        _frame_count++;
        status |= CP_STEP_DISPLAY;
    }
    #endif

    return status;
}

__attribute__((export_name("cp_push_key")))
void cp_push_key(int c) {
    if (_rx_head < 256) {
        _rx_buf[_rx_head++] = (uint8_t)c;
    }
}

__attribute__((export_name("cp_run_repl")))
void cp_run_repl(void) {
    pyexec_event_repl_init();
    fprintf(stderr, "[cp] REPL started\n");
}

__attribute__((export_name("cp_reset")))
void cp_reset(void) {
    /* Lightweight reset between code.py runs / REPL sessions.
     * Keeps VM alive, keeps display alive.  Resets user state. */

    /* TODO: reset pins (hw_state_reset_all) */
    /* TODO: gc_collect to reclaim dead objects */

    _rx_head = 0;
    _rx_tail = 0;

    fprintf(stderr, "[cp] reset\n");
}

__attribute__((export_name("cp_refresh_display")))
int cp_refresh_display(void) {
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_cursor_tick((uint32_t)(mp_hal_ticks_ms() & 0xFFFFFFFF));
    if (worker_terminal_dirty) {
        worker_terminal_refresh();
        worker_terminal_dirty = false;
        _frame_count++;
        return 1;
    }
    #endif
    return 0;
}

/* ------------------------------------------------------------------ */
/* Framebuffer accessors                                               */
/* ------------------------------------------------------------------ */

__attribute__((export_name("cp_get_fb_ptr")))
uintptr_t cp_get_fb_ptr(void) {
    #if CIRCUITPY_DISPLAYIO
    return (uintptr_t)wasm_display_fb_addr();
    #else
    return 0;
    #endif
}

__attribute__((export_name("cp_get_fb_width")))
int cp_get_fb_width(void) {
    #if CIRCUITPY_DISPLAYIO
    return wasm_display_fb_width();
    #else
    return 0;
    #endif
}

__attribute__((export_name("cp_get_fb_height")))
int cp_get_fb_height(void) {
    #if CIRCUITPY_DISPLAYIO
    return wasm_display_fb_height();
    #else
    return 0;
    #endif
}

__attribute__((export_name("cp_get_frame_count")))
uint32_t cp_get_frame_count(void) {
    return _frame_count;
}

/* ------------------------------------------------------------------ */
/* _start — WASI command mode for CLI testing                          */
/* ------------------------------------------------------------------ */

static void _rx_refill(void);  // defined in wasi_mphal.c

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    _core_init();

    fprintf(stderr, "[cp] CircuitPython starting (CLI mode)\n");

    /* Initialize event-driven REPL */
    pyexec_event_repl_init();

    #if CIRCUITPY_DISPLAYIO
    worker_terminal_refresh();
    worker_terminal_dirty = false;
    #endif

    /* Blocking poll loop for CLI/wasmtime use */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16000000 };
    bool running = true;

    while (running) {
        _rx_refill();
        while (_rx_available() > 0) {
            int c = _rx_buf[_rx_tail++];
            if (c == '\n') c = '\r';
            int ret = pyexec_event_repl_process_char(c);
            if (ret & PYEXEC_FORCED_EXIT) {
                running = false;
                break;
            }
        }

        #if CIRCUITPY_DISPLAYIO
        if (worker_terminal_dirty) {
            worker_terminal_refresh();
            worker_terminal_dirty = false;
            _frame_count++;
        }
        #endif

        nanosleep(&ts, NULL);
    }

    mp_deinit();
    return 0;
}

/* ------------------------------------------------------------------ */
/* Required stubs                                                      */
/* ------------------------------------------------------------------ */

void nlr_jump_fail(void *val) {
    (void)val;
    fprintf(stderr, "FATAL: uncaught NLR\n");
    exit(1);
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line,
                            const char *func, const char *expr) {
    (void)func;
    fprintf(stderr, "Assertion '%s' failed, at file %s:%d\n",
            expr, file, line);
    exit(1);
}
#endif
