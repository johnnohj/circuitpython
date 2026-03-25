/*
 * main_worker.c — Hardware peripheral worker for CircuitPython WASI port.
 *
 * Fourth variant alongside standard/opfs/reactor.  Runs as a dedicated
 * Web Worker, owns the display pipeline and peripheral state.  Communicates
 * with the main-thread reactor via OPFS endpoints ("/hw/..." files).
 *
 * Architecture:
 *
 *   Reactor (main thread)                Worker (this binary)
 *   =====================                ====================
 *   User code.py                         main_worker.c boots CircuitPython
 *     displayio shim ──OPFS──▶             with C displayio/terminalio/fontio
 *     digitalio shim ──OPFS──▶             compiled in.
 *     neopixel shim  ──OPFS──▶
 *                                        Optionally runs worker.py for
 *   Reactor is the supervisor:             orchestration, or runs a pure-C
 *     creates /hw/ tree at boot            poll loop if no worker.py present.
 *     signals worker (HUP/TERM/INT)
 *     manages code.py lifecycle          Worker.py (or C loop) polls OPFS
 *                                          endpoints, calls C displayio to
 *                                          composite, writes framebuffer to
 *                                          /hw/display/fb for JS to paint.
 *
 * The worker runs to completion on the C stack (like opfs variant).
 * No yield protocol needed — the poll loop yields cooperatively via
 * time.sleep() in Python or nanosleep via WASI in C.
 *
 * The worker is stateless: no checkpointing, can restart from scratch.
 * The reactor creates the /hw/ endpoint tree and writes the board manifest;
 * the worker discovers endpoints and begins monitoring.
 *
 * Why worker.py exists (but isn't strictly necessary):
 *   - Orchestration layer: signal dispatch, endpoint discovery, driver config
 *   - User-hackable: custom sensor simulation, display layouts
 *   - Same pattern as code.py: C does heavy lifting, Python configures
 *   If worker.py is absent, the C code falls back to a built-in poll loop
 *   that handles display compositing and REPL relay.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
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

/* Where to look for worker.py (in order of preference) */
#define WORKER_SCRIPT_FROZEN  "worker"        /* frozen module name       */
#define WORKER_SCRIPT_PATH    "/lib/worker.py"

/* OPFS endpoint paths */
#define HW_ROOT         "/hw"
#define HW_CONTROL      "/hw/control"
#define HW_DISPLAY_FB   "/hw/display/fb"
#define HW_DISPLAY_SCENE "/hw/display/scene"
#define HW_REPL_TX      "/hw/repl/tx"
#define HW_REPL_RX      "/hw/repl/rx"

/* Control file signal bytes (must match worker.py SIG_* constants) */
#define SIG_NONE    0x00
#define SIG_REFRESH 0x01
#define SIG_RESIZE  0x02
#define SIG_INT     0x03
#define SIG_HUP     0x04
#define SIG_TERM    0x05

/* Control file layout: 32 bytes total
 *   [0]   uint8   signal
 *   [1]   uint8   worker_state
 *   [2:4] uint16  canvas_w
 *   [4:6] uint16  canvas_h
 *   [6:8] uint16  reserved
 *   [8:12] uint32 frame_count
 *   [12:16] uint32 tick_ms
 *   [16:32] pad
 */
#define CTRL_SIZE 32

/* Worker lifecycle states */
#define WS_INIT     0
#define WS_RUNNING  1
#define WS_STOPPING 2
#define WS_STOPPED  3

/* ------------------------------------------------------------------ */
/* Static buffers                                                      */
/* ------------------------------------------------------------------ */

static char heap[MICROPY_GC_HEAP_SIZE];

#if MICROPY_ENABLE_PYSTACK
static mp_obj_t pystack_buf[WORKER_PYSTACK_SIZE / sizeof(mp_obj_t)];
#endif

/* ------------------------------------------------------------------ */
/* Control file I/O                                                    */
/* ------------------------------------------------------------------ */

static void write_control(uint8_t sig, uint8_t state,
                           uint16_t canvas_w, uint16_t canvas_h,
                           uint32_t frame_count, uint32_t tick_ms) {
    uint8_t buf[CTRL_SIZE];
    memset(buf, 0, CTRL_SIZE);
    buf[0] = sig;
    buf[1] = state;
    memcpy(&buf[2], &canvas_w, 2);
    memcpy(&buf[4], &canvas_h, 2);
    memcpy(&buf[8], &frame_count, 4);
    memcpy(&buf[12], &tick_ms, 4);

    int fd = open(HW_CONTROL, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, buf, CTRL_SIZE);
        close(fd);
    }
}

static uint8_t read_signal(void) {
    int fd = open(HW_CONTROL, O_RDONLY);
    if (fd < 0) return SIG_NONE;
    uint8_t sig = SIG_NONE;
    read(fd, &sig, 1);
    close(fd);
    return sig;
}

static void ack_signal(void) {
    int fd = open(HW_CONTROL, O_WRONLY);
    if (fd >= 0) {
        uint8_t zero = 0;
        write(fd, &zero, 1);
        close(fd);
    }
}

/* ------------------------------------------------------------------ */
/* OPFS framebuffer output                                             */
/* ------------------------------------------------------------------ */

static uint32_t _frame_count = 0;

/* ------------------------------------------------------------------ */
/* C fallback poll loop                                                */
/* ------------------------------------------------------------------ */
/* Used when worker.py is not found.  Handles the minimum:             */
/*   - Display: blit scene data to framebuffer, flush to OPFS          */
/*   - REPL relay: forward /hw/repl/rx to reactor                      */
/*   - Signals: process control file                                   */

/* Forward declarations from wasi_mphal.c */
extern void _rx_refill(void);
extern int _rx_available(void);
extern uint8_t _rx_buf[256];
extern int _rx_tail;

static int c_poll_loop(void) {
    fprintf(stderr, "[worker] no worker.py found, running event-driven REPL\n");

    uint16_t canvas_w = 320, canvas_h = 240;
    write_control(SIG_NONE, WS_RUNNING, canvas_w, canvas_h, 0, 0);

    /* Initialize the event-driven REPL.
     * This prints the banner and the first ">>> " prompt.
     * All output goes through mp_hal_stdout_tx_strn → worker_terminal_write. */
    pyexec_event_repl_init();

    /* Force initial display refresh + flush so banner is visible. */
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_refresh();
    worker_terminal_flush();
    worker_terminal_dirty = false;
    #endif

    /* ── Main poll loop ──
     *
     * Clean separation:
     *   1. Read input from OPFS → feed to REPL (non-blocking)
     *   2. Refresh display if content changed → flush to OPFS
     *   3. Check signals
     *   4. Sleep 16ms
     */
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16000000 };
    bool running = true;

    while (running) {
        /* 1. Feed any available keyboard input to REPL */
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

        /* 2. Refresh + flush display only when content changed */
        #if CIRCUITPY_DISPLAYIO
        if (worker_terminal_dirty) {
            worker_terminal_refresh();
            worker_terminal_flush();
            worker_terminal_dirty = false;
            _frame_count++;
        }
        #endif

        /* 3. Sync hardware state (U2IF dispatch handles this now) */

        /* 4. Check signals */
        uint8_t sig = read_signal();
        if (sig == SIG_TERM) {
            ack_signal();
            running = false;
        } else if (sig != SIG_NONE) {
            ack_signal();
        }

        /* 5. Yield */
        nanosleep(&ts, NULL);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Python execution: try worker.py                                     */
/* ------------------------------------------------------------------ */

static int try_run_worker_py(void) {
    nlr_buf_t nlr;

    /* Try frozen module first */
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(
            qstr_from_str(WORKER_SCRIPT_FROZEN),
            mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        (void)mod;  /* import side-effect: worker.main() runs at module level */
        nlr_pop();
        return 0;
    } else {
        /* Frozen module not found — fall through to filesystem */
    }

    /* Try /lib/worker.py */
    struct stat st;
    if (stat(WORKER_SCRIPT_PATH, &st) == 0) {
        int fd = open(WORKER_SCRIPT_PATH, O_RDONLY);
        if (fd < 0) return -1;

        char *src = malloc(st.st_size + 1);
        if (!src) { close(fd); return -1; }
        read(fd, src, st.st_size);
        src[st.st_size] = '\0';
        close(fd);

        int ret = -1;
        if (nlr_push(&nlr) == 0) {
            mp_lexer_t *lex = mp_lexer_new_from_str_len(
                qstr_from_str(WORKER_SCRIPT_PATH),
                src, st.st_size, false);
            mp_parse_tree_t pt = mp_parse(lex, MP_PARSE_FILE_INPUT);
            mp_obj_t fun = mp_compile(&pt, lex->source_name, false);
            mp_call_function_0(fun);
            nlr_pop();
            ret = 0;
        } else {
            mp_obj_print_exception(&mp_stderr_print,
                                   MP_OBJ_FROM_PTR(nlr.ret_val));
        }
        free(src);
        return ret;
    }

    return -1;  /* not found */
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* 1. Init VM */
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(heap, heap + MICROPY_GC_HEAP_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    mp_pystack_init(pystack_buf,
                    pystack_buf + WORKER_PYSTACK_SIZE / sizeof(mp_obj_t));
    #endif

    mp_init();

    /* 2. Mount VFS (WASI preopens provide the underlying fs) */
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

    /* 3. Ensure /hw/ directory tree exists.
     *    The reactor (supervisor) should have created this, but we
     *    create any missing directories defensively.
     */
    mkdir(HW_ROOT, 0777);
    mkdir(HW_ROOT "/display", 0777);
    mkdir(HW_ROOT "/repl", 0777);

    /* 4. Hardware state is managed by worker_u2if.c via postMessage IPC. */

    /* 5. Initialize display + terminal.
     *    Creates WasmFramebuffer + FramebufferDisplay + our TileGrid.
     *    We own the terminal — no supervisor terminal infrastructure. */
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_init();
    #endif

    /* 6. Write initial control file (signals "I'm alive" to main thread) */
    write_control(SIG_NONE, WS_INIT, 320, 240, 0, 0);

    fprintf(stderr, "[worker] CircuitPython hardware worker starting\n");
    fprintf(stderr, "[worker] heap=%dK display=%s\n",
            MICROPY_GC_HEAP_SIZE / 1024,
            #if CIRCUITPY_DISPLAYIO
            "yes"
            #else
            "no"
            #endif
            );

    /* 7. Try to run worker.py; fall back to C poll loop */
    int ret = try_run_worker_py();
    if (ret != 0) {
        ret = c_poll_loop();
    }

    /* 8. Shutdown */
    uint32_t tick = (uint32_t)(mp_hal_ticks_ms() & 0xFFFFFFFF);
    write_control(SIG_NONE, WS_STOPPED, 0, 0, _frame_count, tick);

    fprintf(stderr, "[worker] stopped (frames=%u)\n",
            (unsigned)_frame_count);
    mp_deinit();
    return ret ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Reactor-style exports for postMessage IPC                           */
/* ------------------------------------------------------------------ */
/*
 * When running in a browser via postMessage (no OPFS), JS owns the
 * event loop and calls these exports directly.  The WASM binary is
 * the same — it supports both _start (WASI command, blocking loop)
 * and these exports (JS-driven, non-blocking).
 *
 *   JS calls worker_init() once
 *   JS loop:
 *     worker_push_key(c) for each keyboard char
 *     status = worker_step()
 *     if display dirty:
 *       read framebuffer from worker_get_fb_ptr()
 *       postMessage(fb, [fb.buffer])  // transfer to main
 *
 * The step function does ONE iteration of the poll loop: process
 * input, refresh display, sync hardware.  No sleep, no blocking.
 * Returns a status bitmask.
 */

/* Return values from worker_step */
#define WORKER_STEP_OK          0x00
#define WORKER_STEP_DISPLAY     0x01  /* framebuffer was updated */
#define WORKER_STEP_EXIT        0x02  /* REPL requested exit */

static bool _reactor_initialized = false;

__attribute__((export_name("worker_init")))
int worker_init(void) {
    if (_reactor_initialized) return 0;

    /* Same init sequence as main(), but without the poll loop. */
    mp_cstack_init_with_sp_here(16 * 1024);
    gc_init(heap, heap + sizeof(heap));

    #if MICROPY_ENABLE_PYSTACK
    mp_pystack_init(pystack_buf, pystack_buf + WORKER_PYSTACK_SIZE / sizeof(mp_obj_t));
    #endif

    mp_init();

    /* Mount VFS */
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

    /* Initialize event-driven REPL */
    pyexec_event_repl_init();

    /* Force initial display render */
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_refresh();
    worker_terminal_dirty = false;
    #endif

    _reactor_initialized = true;
    fprintf(stderr, "[worker] reactor mode initialized\n");
    return 0;
}

__attribute__((export_name("worker_step")))
int worker_step(void) {
    int status = WORKER_STEP_OK;

    /* 1. Feed any available keyboard input to REPL */
    while (_rx_available() > 0) {
        int c = _rx_buf[_rx_tail++];
        if (c == '\n') c = '\r';
        int ret = pyexec_event_repl_process_char(c);
        if (ret & PYEXEC_FORCED_EXIT) {
            status |= WORKER_STEP_EXIT;
        }
    }

    /* 2. Tick cursor blink + refresh display if content changed */
    #if CIRCUITPY_DISPLAYIO
    worker_terminal_cursor_tick((uint32_t)(mp_hal_ticks_ms() & 0xFFFFFFFF));
    if (worker_terminal_dirty) {
        worker_terminal_refresh();
        worker_terminal_dirty = false;
        _frame_count++;
        status |= WORKER_STEP_DISPLAY;
    }
    #endif

    return status;
}

__attribute__((export_name("worker_push_key")))
void worker_push_key(int c) {
    /* Push a character into the REPL input buffer.
     * JS calls this for each keyboard event before calling worker_step().
     * The buffer is a simple head/tail pair (not a ring buffer):
     *   _rx_buf[0.._rx_head-1] = pending data
     *   _rx_tail = next read position
     *   _rx_available() = _rx_head - _rx_tail
     */
    extern int _rx_head;
    if (_rx_head < 256) {
        _rx_buf[_rx_head++] = (uint8_t)c;
    }
}

__attribute__((export_name("worker_get_fb_ptr")))
uintptr_t worker_get_fb_ptr(void) {
    #if CIRCUITPY_DISPLAYIO
    return (uintptr_t)wasm_display_fb_addr();
    #else
    return 0;
    #endif
}

__attribute__((export_name("worker_get_fb_width")))
int worker_get_fb_width(void) {
    #if CIRCUITPY_DISPLAYIO
    return wasm_display_fb_width();
    #else
    return 0;
    #endif
}

__attribute__((export_name("worker_get_fb_height")))
int worker_get_fb_height(void) {
    #if CIRCUITPY_DISPLAYIO
    return wasm_display_fb_height();
    #else
    return 0;
    #endif
}

__attribute__((export_name("worker_get_frame_count")))
uint32_t worker_get_frame_count(void) {
    return _frame_count;
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
