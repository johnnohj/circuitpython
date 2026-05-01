// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2015 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/mphalport.h (unix port derivative)
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// Design refs:
//   design/wasm-layer.md                     (wasm layer model, Option A)
//   design/behavior/02-serial-and-stack.md   (serial, interrupt char)
//   design/behavior/06-runtime-environments.md (Node CLI vs browser)
//   design/behavior/07-deviations.md         (no signals, no termios on WASI)
//
// This file declares only the mp_hal_* functions that our wasm layer
// implements directly.  Functions owned by the upstream supervisor
// (mp_hal_delay_ms, mp_hal_stdout_tx_strn, mp_hal_stdin_rx_chr) are
// NOT declared here — they are provided by supervisor/shared/ and our
// port fulfills their contracts via the port_* primitives:
//
//   port_idle_until_interrupt()  → abort-resume yield to JS
//   port_get_raw_ticks()        → WASI clock_gettime
//   port_interrupt_after_ticks() → store deadline for frame loop
//
// See design/wasm-layer.md "mp_hal_* access patterns" for the full
// rationale on which functions are safe to implement directly vs which
// must go through the supervisor.

#pragma once

#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>

// ── Interrupt character ──
// Ctrl-C handling.  On WASI, there are no POSIX signals — the interrupt
// is delivered via the MEMFS rx buffer / background task path.
// See mp_hal_set_interrupt_char() in wasi_mphal.c.

#ifndef CHAR_CTRL_C
#define CHAR_CTRL_C (3)
#endif

void mp_hal_set_interrupt_char(int c);
bool mp_hal_is_interrupted(void);

// ── Atomic sections ──
// On WASM, atomic sections are nesting counters (see mpthreadport.c).
// The frame-budget supervisor checks mp_thread_in_atomic_section()
// before triggering a VM abort — if we're inside one, the abort is
// deferred until the section ends.
//
// On the unix port these are pthread mutex locks.
// On real boards these disable hardware interrupts.
#if MICROPY_PY_THREAD
#define MICROPY_BEGIN_ATOMIC_SECTION() (mp_thread_begin_atomic_section(), 0xffffffff)
#define MICROPY_END_ATOMIC_SECTION(x) (void)x; mp_thread_end_atomic_section()
#endif

// ── stdio ──
// mp_hal_stdio_poll is not implemented (nothing polls stdin on WASM).
// Retained so code that references it still compiles.
#define mp_hal_stdio_poll unused

// Raw/cooked terminal mode.  On WASI these are no-ops (JS host controls
// the terminal).  On native builds (standard board, Linux/macOS) these
// use termios.  Implementations live in wasi_mphal.c.
void mp_hal_stdio_mode_raw(void);
void mp_hal_stdio_mode_orig(void);

// ── Readline ──
// Used by the standard (CLI) board when MICROPY_USE_READLINE == 1.
// The browser board does not use this path — JS owns readline there.
#if MICROPY_PY_BUILTINS_INPUT && MICROPY_USE_READLINE == 1

#include "py/misc.h"
#include "shared/readline/readline.h"

#define mp_hal_readline mp_hal_readline
static inline int mp_hal_readline(vstr_t *vstr, const char *p) {
    mp_hal_stdio_mode_raw();
    int ret = readline(vstr, p);
    mp_hal_stdio_mode_orig();
    return ret;
}

#endif

// ── Timing ──
// mp_hal_delay_us: short microsecond delays for hardware timing.
// These are short enough not to blow the frame budget, so nanosleep
// (which WASI provides) is fine.
//
// mp_hal_delay_ms is NOT defined here.  The upstream supervisor provides
// it in supervisor/shared/tick.c.  Its delay loop calls:
//   - RUN_BACKGROUND_TASKS      (we get background task execution for free)
//   - mp_hal_is_interrupted()    (Ctrl-C checking for free)
//   - port_interrupt_after_ticks (we store the deadline)
//   - port_idle_until_interrupt  (we abort-resume back to JS)
//
// On real boards, port_idle_until_interrupt puts the CPU to sleep and
// a timer interrupt wakes it.  For us, "sleep" = return to JS via
// abort-resume, "timer interrupt" = next frame callback re-enters C.
static inline void mp_hal_delay_us(mp_uint_t us) {
    #ifdef __wasi__
    struct timespec ts = {
        .tv_sec = us / 1000000,
        .tv_nsec = (us % 1000000) * 1000
    };
    nanosleep(&ts, NULL);
    #else
    usleep(us);
    #endif
}

#define mp_hal_ticks_cpu() 0

// ── Syscall retry ──
// PEP 475: retry syscalls interrupted by signals (EINTR).
// GIL macros expand to no-ops on our single-threaded port, but the
// EINTR retry is still useful for WASI syscalls.
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) { \
        for (;;) { \
            MP_THREAD_GIL_EXIT(); \
            ret = syscall; \
            MP_THREAD_GIL_ENTER(); \
            if (ret == -1) { \
                int err = errno; \
                if (err == EINTR) { \
                    mp_handle_pending(true); \
                    continue; \
                } \
                raise; \
            } \
            break; \
        } \
}

#define RAISE_ERRNO(err_flag, error_val) \
    { if (err_flag == -1) \
      { mp_raise_OSError(error_val); } }

// ── Random ──
// Implementation in wasi_mphal.c (reads /dev/random via WASI).
void mp_hal_get_random(size_t n, void *buf);
