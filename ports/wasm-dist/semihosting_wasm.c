/*
 * semihosting_wasm.c — WASM adaptation of ARM semihosting
 *
 * Routes semihosting I/O to Emscripten MEMFS virtual devices:
 *   SYS_WRITE (fd 1) → /dev/stdout  (Python print, mp_hal_stdout_tx_strn)
 *   SYS_WRITE (fd 2) → /dev/stderr
 *   SYS_READ  (fd 0) → /dev/stdin
 *   SYS_CLOCK         → mp_js_ticks_ms()
 *   SYS_EXIT          → mp_sched_vm_abort() + write exit code
 *
 * See semihosting_wasm.h for public API documentation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "semihosting_wasm.h"
#include "vdev.h"
#include "py/mphal.h"
#include "py/runtime.h"

/* ---- init ---- */

void mp_semihosting_init(void) {
    /* vdev_init() must have already been called; FDs are already open. */
    /* Nothing extra needed here — kept for API parity with semihosting_arm. */
}

/* ---- exit ---- */

void mp_semihosting_exit(int status) {
    /* Write exit status to /state/result.json via a simple JSON patch */
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"delta\":{},\"stdout\":\"\",\"stderr\":\"\","
        "\"aborted\":true,\"exit_status\":%d,\"duration_ms\":0,\"frames\":[]}",
        status);
    int fd = open("/state/result.json", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        write(fd, buf, strlen(buf));
        close(fd);
    }
    mp_sched_vm_abort();
}

/* ---- stdout / stderr ---- */

void mp_semihosting_tx_strn(const char *str, size_t len) {
    if (vdev_stdout_fd >= 0) {
        write(vdev_stdout_fd, str, len);
    }
}

void mp_semihosting_tx_strn_cooked(const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\n') {
            mp_semihosting_tx_strn("\r\n", 2);
        } else {
            mp_semihosting_tx_strn(&str[i], 1);
        }
    }
}

/* ---- stdin ---- */

int mp_semihosting_rx_char(void) {
    char c;
    while (1) {
        if (vdev_stdin_fd >= 0) {
            ssize_t n = read(vdev_stdin_fd, &c, 1);
            if (n == 1) {
                return (unsigned char)c;
            }
        }
        /* stdin is empty — yield via VM hook so BC messages can arrive */
        mp_js_hook();

        /* Check for keyboard interrupt */
        if (mp_hal_get_interrupt_char() == (char)3) {
            mp_sched_keyboard_interrupt();
        }

        /* Re-seek stdin to start for next read attempt */
        if (vdev_stdin_fd >= 0) {
            lseek(vdev_stdin_fd, 0, SEEK_SET);
        }
    }
}

int mp_semihosting_rx_chars(char *str, size_t len) {
    size_t count = 0;
    while (count < len) {
        int c = mp_semihosting_rx_char();
        if (c < 0) {
            break;
        }
        str[count++] = (char)c;
        if (c == '\n' || c == '\r') {
            break;
        }
    }
    return (int)count;
}

/* ---- debug log ---- */

void mp_semihosting_debug_write(const char *str, size_t len) {
    int fd = open("/debug/semihosting.log", O_WRONLY | O_APPEND | O_CREAT, 0666);
    if (fd >= 0) {
        write(fd, str, len);
        close(fd);
    }
}
