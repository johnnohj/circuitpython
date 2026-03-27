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


int mp_hal_stdin_rx_chr(void) {
    // Blocking read from WASI stdin (fd 0).
    // Used by standard variant and main() CLI mode.
    // The event-driven REPL (MICROPY_REPL_EVENT_DRIVEN) bypasses this
    // entirely — JS pushes chars via cp_push_key → _rx_buf.
    unsigned char c;
    ssize_t ret;
    MP_HAL_RETRY_SYSCALL(ret, read(STDIN_FILENO, &c, 1), {});
    if (ret == 0) {
        c = 4; // EOF → ctrl-D
    } else if (c == '\n') {
        c = '\r';
    }
    return c;
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

    // Route output to supervisor terminal (VT100 + display rendering).
    #if CIRCUITPY_TERMINALIO
    if (supervisor_terminal_started()) {
        int errcode;
        common_hal_terminalio_terminal_write(
            &supervisor_terminal, (const uint8_t *)str, len, &errcode);
        #if CIRCUITPY_DISPLAYIO
        extern bool worker_terminal_dirty;
        worker_terminal_dirty = true;
        #endif
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

// ---- Native WASM compilation stub ----
// emitglue.c calls mp_wasm_compile_native() to build a WASM module from
// emitted native code.  In a browser, the JS host provides the real
// implementation (reads WASM linear memory, builds WebAssembly.Module,
// adds to function table).  In CLI/test mode (wasmtime, Node without
// the JS runtime), this stub returns 0 → the compiler falls back to
// bytecode interpretation.  The user's code still works; it just
// runs interpreted instead of native.
#if MICROPY_EMIT_WASM
__attribute__((weak))
int mp_wasm_compile_native(const void *code, size_t len) {
    (void)code;
    (void)len;
    return 0;  // 0 = compilation failed, fall back to bytecode
}
#endif

// CircuitPython stubs (reset_into_safe_mode, stack_ok, assert_heap_ok,
// decompress, serial_write_compressed) are provided by:
//   supervisor/stub/safe_mode.c
//   supervisor/stub/stack.c
//   supervisor/shared/translate/translate.c
// Included in all variants via SRC_SUPERVISOR in the Makefile.
