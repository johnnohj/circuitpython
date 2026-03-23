/*
 * WASI port HAL implementation
 *
 * Based on unix port's unix_mphal.c.
 * Stripped: signals, termios/raw TTY, dupterm, getrandom, threading.
 * All I/O via WASI fd_read/fd_write (POSIX read/write).
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "py/mphal.h"
#include "py/runtime.h"

#if CIRCUITPY_TERMINALIO
#include "supervisor/shared/display.h"
#include "shared-bindings/terminalio/Terminal.h"
#include "shared-bindings/framebufferio/FramebufferDisplay.h"
#include "shared-module/displayio/__init__.h"
#include "board_display.h"
#include "wasm_framebuffer.h"
#endif

#if MICROPY_WORKER
#include "hw_opfs.h"
#endif

// mp_hal_set_interrupt_char and mp_hal_is_interrupted are provided
// by shared/runtime/interrupt_char.c

// ---- stdin ----

// OPFS-backed stdin buffer for the worker variant.
// JS writes keyboard bytes to /hw/repl/rx; we consume them here.
#define REPL_RX_PATH "/hw/repl/rx"
uint8_t _rx_buf[256];
int _rx_head = 0;
int _rx_tail = 0;

int _rx_available(void) {
    return _rx_head - _rx_tail;
}

void _rx_refill(void) {
    struct stat st;
    if (stat(REPL_RX_PATH, &st) != 0 || st.st_size == 0) {
        return;
    }
    int fd = open(REPL_RX_PATH, O_RDONLY);
    if (fd < 0) return;
    int n = read(fd, _rx_buf, sizeof(_rx_buf));
    close(fd);
    if (n > 0) {
        _rx_head = n;
        _rx_tail = 0;
        // Consume: truncate the file
        fd = open(REPL_RX_PATH, O_WRONLY | O_TRUNC, 0666);
        if (fd >= 0) close(fd);
    }
}

#if CIRCUITPY_DISPLAYIO
bool _display_content_dirty = false;

static void _refresh_display(void) {
    if (!_display_content_dirty) {
        return;  /* Nothing changed since last refresh */
    }
    framebufferio_framebufferdisplay_obj_t *fbdisp =
        &displays[0].framebuffer_display;
    if (fbdisp->base.type == &framebufferio_framebufferdisplay_type) {
        fbdisp->core.full_refresh = true;
        common_hal_framebufferio_framebufferdisplay_refresh(fbdisp, 0, 0);
    }
    _display_content_dirty = false;
}

/* Called by mp_hal_stdout_tx_strn when text is written to the terminal */
static void _mark_display_dirty(void) {
    _display_content_dirty = true;
}

// Flush the WasmFramebuffer to OPFS (worker variant only)
static void _flush_fb_to_opfs(void) {
    #if MICROPY_WORKER
    uint8_t *fb_addr = wasm_display_fb_addr();
    int w = wasm_display_fb_width();
    int h = wasm_display_fb_height();
    size_t fb_size = (size_t)w * h * 2;
    /* Open+write+close each time to ensure mtime updates. */
    int fd = open("/hw/display/fb", O_WRONLY | O_CREAT, 0666);
    if (fd >= 0) {
        write(fd, fb_addr, fb_size);
        close(fd);
    }
    #endif
}
#endif

int mp_hal_stdin_rx_chr(void) {
    #if MICROPY_WORKER
    // Worker variant: poll /hw/repl/rx for keyboard input.
    // While waiting, keep the display refreshed.
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 16000000 };  // 16ms (~60fps)

    for (;;) {
        // Check buffer first
        if (_rx_available() > 0) {
            uint8_t c = _rx_buf[_rx_tail++];
            if (c == '\n') c = '\r';
            return c;
        }

        // Try to refill from OPFS
        _rx_refill();
        if (_rx_available() > 0) {
            continue;  // Got data, loop back to return it
        }

        // No input — refresh + flush display only if content changed
        #if CIRCUITPY_DISPLAYIO
        if (_display_content_dirty) {
            _refresh_display();  /* clears _display_content_dirty */
            _flush_fb_to_opfs();
        }
        #endif

        // Also sync hardware state
        #if MICROPY_WORKER
        hw_opfs_read_all();
        hw_opfs_flush_all();
        #endif

        nanosleep(&ts, NULL);
    }
    #else
    // Standard/reactor variant: read from WASI stdin (fd 0)
    unsigned char c;
    ssize_t ret;
    MP_HAL_RETRY_SYSCALL(ret, read(STDIN_FILENO, &c, 1), {});
    if (ret == 0) {
        c = 4; // EOF → ctrl-D
    } else if (c == '\n') {
        c = '\r';
    }
    return c;
    #endif
}

// ---- stdout ----
// Writes to BOTH fd 1 (UART serial / wasmtime console) AND /dev/repl
// (USB serial / xterm.js). Both coexist, like a real CircuitPython board.

#ifdef MICROPY_OPFS_EXECUTOR
#include "dev_repl.h"
#endif

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    ssize_t ret;
    MP_HAL_RETRY_SYSCALL(ret, write(STDOUT_FILENO, str, len), {});

    #ifdef MICROPY_OPFS_EXECUTOR
    // Also write to /dev/repl stdout ring
    dev_repl_stdout_write(str, len);
    #endif

    // Worker: route ALL output to our terminal (clean path, no supervisor)
    #if MICROPY_WORKER
    #include "worker_terminal.h"
    worker_terminal_write(str, len);
    #elif CIRCUITPY_TERMINALIO
    // Non-worker variants: use supervisor terminal if available
    if (supervisor_terminal_started()) {
        int errcode;
        common_hal_terminalio_terminal_write(
            &supervisor_terminal, (const uint8_t *)str, len, &errcode);
    }
    #endif

    return ret < 0 ? 0 : (mp_uint_t)ret;
}

// mp_hal_stdout_tx_strn_cooked and mp_hal_stdout_tx_str are provided
// by shared/runtime/stdout_helpers.c

// ---- stderr ----

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    write(STDERR_FILENO, str, len);
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};

// ---- Timing ----

mp_uint_t mp_hal_ticks_ms(void) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

mp_uint_t mp_hal_ticks_us(void) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}

uint64_t mp_hal_time_ns(void) {
    struct timespec tv;
    clock_gettime(CLOCK_REALTIME, &tv);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_nsec;
}

// mp_hal_delay_ms is provided by supervisor/shared/tick.c for both variants.

// ---- Random ----

void mp_hal_get_random(size_t n, void *buf) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, buf, n);
        close(fd);
    } else {
        memset(buf, 0, n);
    }
}

// ---- supervisor_ticks_ms ----
// Required by extmod/modasyncio.c when CIRCUITPY=1 and not unix/apple.
// Matches shared-bindings/supervisor/__init__.c: wraps to 29-bit range.
mp_obj_t supervisor_ticks_ms(void) {
    uint64_t ms = (uint64_t)mp_hal_ticks_ms();
    return mp_obj_new_int((mp_int_t)((ms + 0x1fff0000) % (1 << 29)));
}

// CircuitPython stubs (reset_into_safe_mode, stack_ok, assert_heap_ok,
// decompress, serial_write_compressed) are provided by:
//   supervisor/stub/safe_mode.c
//   supervisor/stub/stack.c
//   supervisor/shared/translate/translate.c
// Included in all variants via SRC_SUPERVISOR in the Makefile.
