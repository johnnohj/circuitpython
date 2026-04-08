/*
 * supervisor/serial.c — Port-local serial multiplexer for WASM.
 *
 * Replaces supervisor/shared/serial.c with WASM-specific channels.
 * Same interface (serial.h), different wiring:
 *
 *   serial_write_substring()
 *     ├── display path: supervisor terminal (displayio framebuffer)
 *     └── console path: write(STDOUT_FILENO) → WASI stdout
 *         (CLI: real terminal; browser: wasi-memfs.js → onStdout)
 *
 *   serial_read()
 *     ├── rx ring buffer (browser: JS pushes via event ring)
 *     └── read(STDIN_FILENO) (CLI fallback)
 *
 * Owns the rx buffer.  supervisor.c pushes bytes via serial_push_byte()
 * (from sh_on_event) and checks for Ctrl-C via serial_check_interrupt()
 * (from wasm_background_tasks).
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "py/mpconfig.h"
#include "py/runtime.h"
#include "supervisor/shared/serial.h"

#if CIRCUITPY_TERMINALIO
#include "shared-bindings/terminalio/Terminal.h"
#include "supervisor/shared/display.h"
#endif

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static bool _console_write_disabled = false;
static bool _display_write_disabled = false;

/* Runtime mode — set by main() in supervisor.c */
extern bool wasm_cli_mode;

/* ------------------------------------------------------------------ */
/* Rx buffer — keyboard input from JS event ring or CLI stdin          */
/*                                                                     */
/* sh_on_event(KEY_DOWN) pushes bytes via serial_push_byte().          */
/* serial_read() consumes.  serial_check_interrupt() scans for Ctrl-C. */
/* ------------------------------------------------------------------ */

#define RX_BUF_SIZE 256
static uint8_t _rx_buf[RX_BUF_SIZE];
static int _rx_head = 0;
static int _rx_tail = 0;

static int _rx_available(void) {
    return _rx_head - _rx_tail;
}

void serial_push_byte(uint8_t c) {
    if (_rx_head < RX_BUF_SIZE) {
        _rx_buf[_rx_head++] = c;
    }
}

void serial_check_interrupt(void) {
    for (int i = _rx_tail; i < _rx_head; i++) {
        if (_rx_buf[i] == 3) {
            mp_sched_keyboard_interrupt();
            break;
        }
    }
}

static char _consume_byte(void) {
    unsigned char c = _rx_buf[_rx_tail++];
    /* Reset buffer when fully consumed */
    if (_rx_tail >= _rx_head) {
        _rx_head = 0;
        _rx_tail = 0;
    }
    if (c == '\n') {
        c = '\r';
    }
    return (char)c;
}

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

void serial_early_init(void) {
}

void serial_init(void) {
}

/* ------------------------------------------------------------------ */
/* Connection status                                                   */
/* ------------------------------------------------------------------ */

bool serial_connected(void) {
    /* Always connected — JS host is present, or we're in CLI mode. */
    return true;
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

uint32_t serial_write_substring(const char *text, uint32_t length) {
    if (length == 0) {
        return 0;
    }

    uint32_t length_sent = length;

    /* Display path: render to supervisor terminal (displayio framebuffer). */
    #if CIRCUITPY_TERMINALIO
    if (!_display_write_disabled) {
        int errcode;
        length_sent = common_hal_terminalio_terminal_write(
            &supervisor_terminal, (const uint8_t *)text, length, &errcode);
    }
    #endif

    if (_console_write_disabled) {
        return length_sent;
    }

    /* Console path: write to WASI stdout.
     * In CLI mode, this goes to the real terminal.
     * In browser mode, wasi-memfs.js intercepts fd_write on fd 1
     * and calls onStdout — which feeds the serial monitor div. */
    {
        ssize_t ret = write(STDOUT_FILENO, text, length);
        (void)ret;
    }

    return length_sent;
}

void serial_write(const char *text) {
    serial_write_substring(text, strlen(text));
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

uint32_t serial_bytes_available(void) {
    return (uint32_t)_rx_available();
}

char serial_read(void) {
    /* Check rx buffer first (browser: JS pushed keys via event ring) */
    if (_rx_available() > 0) {
        return _consume_byte();
    }

    /* CLI fallback: blocking read from WASI stdin */
    if (wasm_cli_mode) {
        unsigned char c;
        ssize_t ret = read(STDIN_FILENO, &c, 1);
        if (ret <= 0) {
            return 4; /* EOF → Ctrl-D */
        }
        if (c == '\n') {
            c = '\r';
        }
        return (char)c;
    }

    return -1;
}

/* ------------------------------------------------------------------ */
/* Write control — match supervisor/shared/serial.c interface          */
/* ------------------------------------------------------------------ */

bool serial_console_write_disable(bool disabled) {
    bool prev = _console_write_disabled;
    _console_write_disabled = disabled;
    return prev;
}

bool serial_display_write_disable(bool disabled) {
    bool prev = _display_write_disabled;
    _display_write_disabled = disabled;
    return prev;
}

/* ------------------------------------------------------------------ */
/* board_serial_* — weak defaults (board-level overrides)              */
/* ------------------------------------------------------------------ */

MP_WEAK void board_serial_early_init(void) {}
MP_WEAK void board_serial_init(void) {}
MP_WEAK bool board_serial_connected(void) { return false; }
MP_WEAK char board_serial_read(void) { return -1; }
MP_WEAK uint32_t board_serial_bytes_available(void) { return 0; }
MP_WEAK void board_serial_write_substring(const char *text, uint32_t length) {
    (void)text; (void)length;
}
