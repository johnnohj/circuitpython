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

#include "py/mphal.h"
#include "py/runtime.h"

// mp_hal_set_interrupt_char and mp_hal_is_interrupted are provided
// by shared/runtime/interrupt_char.c

// ---- stdin ----

int mp_hal_stdin_rx_chr(void) {
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

// mp_hal_delay_ms is provided by supervisor/shared/tick.c (OPFS variant)
// or can be defined here for the standard variant:
#ifndef MICROPY_OPFS_EXECUTOR
void mp_hal_delay_ms(mp_uint_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}
#endif

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

// CircuitPython stubs (reset_into_safe_mode, stack_ok, assert_heap_ok,
// decompress, serial_write_compressed) are provided by:
//   supervisor/stub/safe_mode.c
//   supervisor/stub/stack.c
//   supervisor/shared/translate/translate.c
// Included in all variants via SRC_SUPERVISOR in the Makefile.
