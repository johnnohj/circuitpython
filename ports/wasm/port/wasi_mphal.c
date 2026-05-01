// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/wasi_mphal.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/wasi_mphal.c — WASI platform services for the WASM port.
//
// Provides timing, random, interrupt char, and stdio mode.
// Part of the wasm layer — co-located with hal.c (same substrate,
// different contract).
//
// Option A (decided): the upstream supervisor owns mp_hal_delay_ms
// (supervisor/shared/tick.c).  We do NOT implement it here.  Instead,
// we implement the primitives the supervisor calls:
//   - port_get_raw_ticks()       → WASI clock_gettime
//   - port_idle_until_interrupt() → abort-resume (vm_abort.c)
//   - port_interrupt_after_ticks() → store deadline
//
// Functions we own directly (safe, no supervisor mediation):
//   - mp_hal_ticks_ms/us, mp_hal_time_ns
//   - mp_hal_set_interrupt_char
//   - mp_hal_is_interrupted
//   - mp_hal_get_random
//   - mp_hal_stdio_mode_raw/orig
//
// Design refs:
//   design/wasm-layer.md                    (Option A, mp_hal_* access)
//   design/behavior/02-serial-and-stack.md  (interrupt char)
//   design/behavior/07-deviations.md        (no signals, no termios)

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>

#include "py/mphal.h"
#include "py/mpthread.h"
#include "py/runtime.h"
#include "port/port_memory.h"

// ── stderr printer ──
// Always goes to WASI fd 2.  Used by MICROPY_DEBUG_PRINTER.

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    write(STDERR_FILENO, str, len);
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};

// ── Interrupt character ──
// On WASI, there are no POSIX signals — interrupt is delivered via
// the MEMFS rx buffer / background task path.

void mp_hal_set_interrupt_char(int c) {
    (void)c;
    // No signal handler on WASI.  Ctrl-C is delivered via the event
    // ring (EVT_KEY_DOWN with keycode 3) → serial_check_interrupt.
}

bool mp_hal_is_interrupted(void) {
    return MP_STATE_THREAD(mp_pending_exception) != MP_OBJ_NULL;
}

// ── stdio mode ──
// On WASI, JS host controls the terminal — no termios.

#if MICROPY_USE_READLINE == 1
#if defined(__wasi__)
void mp_hal_stdio_mode_raw(void) {}
void mp_hal_stdio_mode_orig(void) {}
#else
#include <termios.h>

static struct termios orig_termios;

void mp_hal_stdio_mode_raw(void) {
    tcgetattr(0, &orig_termios);
    static struct termios termios;
    termios = orig_termios;
    termios.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    termios.c_cflag = (termios.c_cflag & ~(CSIZE | PARENB)) | CS8;
    termios.c_lflag = 0;
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSAFLUSH, &termios);
}

void mp_hal_stdio_mode_orig(void) {
    tcsetattr(0, TCSAFLUSH, &orig_termios);
}
#endif
#endif

// ── Timing ──
// These are pure time queries — safe to own directly (Option A).

#ifndef mp_hal_ticks_ms
mp_uint_t mp_hal_ticks_ms(void) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}
#endif

#ifndef mp_hal_ticks_us
mp_uint_t mp_hal_ticks_us(void) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
}
#endif

#ifndef mp_hal_time_ns
uint64_t mp_hal_time_ns(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}
#endif

// NOTE: mp_hal_delay_ms is NOT defined here.
// The upstream supervisor provides it in supervisor/shared/tick.c.
// It calls port_idle_until_interrupt() (in vm_abort.c) when idle.
// See design/wasm-layer.md "Option A".

// ── Supervisor timing primitives (Option A) ──

// port_get_raw_ticks: return time in 1/1024-second ticks.
// The upstream supervisor's mp_hal_delay_ms converts between ms and
// these ticks.  We use CLOCK_MONOTONIC for the source.
uint64_t port_get_raw_ticks(uint8_t *subticks) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    // Convert to 1/1024-second ticks
    uint64_t total_subticks = (uint64_t)tv.tv_sec * 1024
                            + (uint64_t)tv.tv_nsec * 1024 / 1000000000ULL;
    if (subticks != NULL) {
        *subticks = (uint8_t)(total_subticks & 0x1F);  // lower 5 bits
    }
    return (uint32_t)(total_subticks >> 5);
}

// Store the wakeup deadline in port_mem so JS knows when to
// schedule the next frame.  Converts from 1/1024-sec ticks to ms.
// The upstream mp_hal_delay_ms loop calls this before
// port_idle_until_interrupt().

void port_interrupt_after_ticks(uint32_t ticks) {
    // Convert ticks (1/1024 sec) to ms delay from now
    uint32_t delay_ms = (ticks * 1000) / 1024;
    port_mem.wakeup_ms = delay_ms;
}

// port_idle_until_interrupt is in vm_abort.c (abort-resume yield).

// ── Random ──

void mp_hal_get_random(size_t n, void *buf) {
    int fd = open("/dev/random", O_RDONLY);
    if (fd >= 0) {
        read(fd, buf, n);
        close(fd);
    } else {
        // Fallback: zero-fill if /dev/random unavailable
        memset(buf, 0, n);
    }
}
