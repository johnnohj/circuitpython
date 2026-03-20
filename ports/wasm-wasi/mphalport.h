/*
 * WASI port HAL interface
 *
 * Stripped-down version of the unix port's mphalport.h.
 * No signals, no raw TTY mode, no threading.
 */
#pragma once

#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

#ifndef CHAR_CTRL_C
#define CHAR_CTRL_C (3)
#endif

void mp_hal_set_interrupt_char(int c);

// No raw/orig TTY mode in WASI (no termios)
static inline void mp_hal_stdio_mode_raw(void) { }
static inline void mp_hal_stdio_mode_orig(void) { }

#define mp_hal_stdio_poll unused

static inline void mp_hal_delay_us(mp_uint_t us) {
    struct timespec ts = { .tv_sec = us / 1000000, .tv_nsec = (us % 1000000) * 1000 };
    nanosleep(&ts, NULL);
}

#define mp_hal_ticks_cpu() 0

// PEP 475: retry syscalls on EINTR
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) { \
        for (;;) { \
            ret = syscall; \
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
