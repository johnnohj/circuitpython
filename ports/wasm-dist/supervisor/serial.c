/*
 * supervisor/serial.c — Port-local serial multiplexer for WASM.
 *
 * Replaces supervisor/shared/serial.c with WASM-specific channels.
 * Same interface (serial.h), different wiring:
 *
 *   serial_write_substring()
 *     ├── display path: supervisor terminal (future: displayio framebuffer)
 *     ├── console path: /hal/serial/tx fd (future: wasi-memfs.js → xterm.js)
 *     └── CLI path: write(STDOUT_FILENO) for wasmtime/node testing
 *
 *   serial_read()
 *     ├── rx ring buffer (browser: JS pushes via cp_push_key)
 *     └── read(STDIN_FILENO) (CLI fallback)
 *
 * We own the entire I/O path — no USB, BLE, UART, or WebSocket deps.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "py/mpconfig.h"
#include "supervisor/shared/serial.h"

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static bool _console_write_disabled = false;
static bool _display_write_disabled = false;

/* Runtime mode — set by main() in supervisor.c */
extern bool wasm_cli_mode;

/* rx buffer — owned by supervisor.c */
extern uint8_t _rx_buf[];
extern int _rx_head;
extern int _rx_tail;
extern int _rx_available(void);

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

    /* Display path: render to supervisor terminal (displayio).
     * TODO: wire common_hal_terminalio_terminal_write when display is enabled.
     */
    // if (!_display_write_disabled) {
    //     terminal_write(text, length);
    // }

    if (_console_write_disabled) {
        return length;
    }

    /* Console path: write to /hal/serial/tx fd.
     * TODO: open /hal/serial/tx at init, write to that fd here.
     * For now, in browser mode this is where xterm.js output will go.
     */

    /* CLI path: write to WASI stdout for wasmtime/node. */
    if (wasm_cli_mode) {
        ssize_t ret = write(STDOUT_FILENO, text, length);
        (void)ret;
    }

    return length;
}

void serial_write(const char *text) {
    serial_write_substring(text, strlen(text));
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

uint32_t serial_bytes_available(void) {
    uint32_t count = _rx_available();

    /* Future: also check /hal/serial/rx fd */

    return count;
}

char serial_read(void) {
    /* Check rx buffer first (browser: JS pushed keys via cp_push_key) */
    if (_rx_available() > 0) {
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
/* port_serial_* — CIRCUITPY_PORT_SERIAL interface                     */
/*                                                                     */
/* These are called by supervisor/shared/serial.c when                 */
/* CIRCUITPY_PORT_SERIAL=1.  Since we provide our own serial.c,        */
/* these also serve as the backing implementation for our channels.     */
/* ------------------------------------------------------------------ */

void port_serial_early_init(void) {}
void port_serial_init(void) {}

bool port_serial_connected(void) {
    return true;
}

char port_serial_read(void) {
    return serial_read();
}

uint32_t port_serial_bytes_available(void) {
    return serial_bytes_available();
}

void port_serial_write_substring(const char *text, uint32_t length) {
    serial_write_substring(text, length);
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
