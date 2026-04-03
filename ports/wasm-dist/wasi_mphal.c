/*
 * wasi_mphal.c — WASI platform services for the WASM port.
 *
 * Provides timing, random, interrupt char, stdio mode, and stderr.
 * Stdin/stdout are handled by supervisor/micropython.c + supervisor/serial.c.
 *
 * Adapted from ports/unix/unix_mphal.c with WASI guards for signals/termios.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>

#include "py/mphal.h"
#include "py/mpthread.h"
#include "py/runtime.h"
#include "py/obj.h"

/* ------------------------------------------------------------------ */
/* stderr printer — always goes to WASI fd 2                           */
/* ------------------------------------------------------------------ */

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    write(STDERR_FILENO, str, len);
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};

/* ------------------------------------------------------------------ */
/* Interrupt character                                                 */
/* ------------------------------------------------------------------ */

#if !defined(_WIN32) && !defined(__wasi__)
#include <signal.h>

static void sighandler(int signum) {
    if (signum == SIGINT) {
        #if MICROPY_ASYNC_KBD_INTR
        #error "MICROPY_ASYNC_KBD_INTR not supported on this port"
        #else
        if (MP_STATE_MAIN_THREAD(mp_pending_exception) == MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_kbd_exception))) {
            exit(1);
        }
        mp_sched_keyboard_interrupt();
        #endif
    }
}
#endif

void mp_hal_set_interrupt_char(int c) {
    #if defined(__wasi__)
    (void)c; /* Interrupt via MEMFS rx buffer / background task */
    #elif !defined(_WIN32)
    if (c == CHAR_CTRL_C) {
        struct sigaction sa;
        sa.sa_flags = 0;
        sa.sa_handler = sighandler;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
    } else {
        struct sigaction sa;
        sa.sa_flags = 0;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
    }
    #endif
}

bool mp_hal_is_interrupted(void) {
    return false;
}

/* ------------------------------------------------------------------ */
/* stdio mode — terminal raw/cooked control                            */
/* ------------------------------------------------------------------ */

#if MICROPY_USE_READLINE == 1
#if defined(__wasi__)
/* WASI has no termios — JS host controls terminal mode. */
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

/* ------------------------------------------------------------------ */
/* Timing — CLOCK_MONOTONIC via wasi-sdk                               */
/* ------------------------------------------------------------------ */

/* JS time source — written by cp_step() in supervisor.c each frame.
 * Zero means CLI mode (cp_step not called), fall back to clock_gettime. */
extern volatile uint64_t wasm_js_now_ms;

#ifndef mp_hal_ticks_ms
mp_uint_t mp_hal_ticks_ms(void) {
    if (wasm_js_now_ms != 0) {
        return (mp_uint_t)wasm_js_now_ms;
    }
    /* CLI mode fallback */
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}
#endif

#ifndef mp_hal_ticks_us
mp_uint_t mp_hal_ticks_us(void) {
    #if (defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0) && defined(_POSIX_MONOTONIC_CLOCK)
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return tv.tv_sec * 1000000 + tv.tv_nsec / 1000;
    #else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
    #endif
}
#endif

#ifndef mp_hal_time_ns
uint64_t mp_hal_time_ns(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000000ULL + (uint64_t)tv.tv_usec * 1000ULL;
}
#endif

#ifndef mp_hal_delay_ms
void mp_hal_delay_ms(mp_uint_t ms) {
    mp_uint_t start = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - start < ms) {
        mp_event_wait_ms(1);
    }
}
#endif

/* supervisor_ticks_ms, supervisor_ticks_ms64, supervisor_ticks_ms32
 * are now provided by supervisor/tick.c */

/* ------------------------------------------------------------------ */
/* Random                                                              */
/* ------------------------------------------------------------------ */

void mp_hal_get_random(size_t n, void *buf) {
    int fd = open("/dev/random", O_RDONLY);
    if (fd >= 0) {
        read(fd, buf, n);
        close(fd);
    } else {
        /* Fallback: zero-fill if /dev/random unavailable */
        memset(buf, 0, n);
    }
}
