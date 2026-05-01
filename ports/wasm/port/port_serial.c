// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on CircuitPython port conventions (ports/raspberrypi/, ports/atmel-samd/)
// SPDX-FileCopyrightText: Contributions by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/port_serial.c — Port serial channel via port_mem ring buffers.
//
// Implements the port_serial_*() weak functions declared in
// supervisor/shared/serial.h.  The upstream multiplexer calls these
// alongside USB CDC, BLE, WebSocket, etc.  For our port, the ring
// buffers in port_mem ARE the serial connection — JS writes to
// serial_rx (keystrokes), C reads from it; C writes to serial_tx
// (REPL output), JS reads from it.
//
// Design refs:
//   design/io-channels.md       (unified bus model)
//   design/packet-protocol.md   (per-frame data contract)
//   design/workflow-analog.md   (USB analog)

#include <unistd.h>

#include "supervisor/shared/serial.h"
#include "port/serial.h"
#include "port/port_memory.h"

void port_serial_early_init(void) {
    // Ring buffers are zero-initialized as part of port_mem.
    // Nothing else to do.
}

void port_serial_init(void) {
    // No runtime init needed — rings are ready from port_mem init.
}

bool port_serial_connected(void) {
    // The JS host is always present.  On a real board this would
    // check USB cable state; for us, if WASM is running then JS
    // is connected.
    return true;
}

uint32_t port_serial_bytes_available(void) {
    return (uint32_t)serial_rx_available();
}

char port_serial_read(void) {
    uint8_t c;
    size_t n = serial_rx_read(&c, 1);
    if (n == 0) {
        return -1;
    }
    // Convert \n to \r for readline compatibility.
    // The upstream REPL expects \r as the line terminator.
    if (c == '\n') {
        c = '\r';
    }
    return (char)c;
}

void port_serial_write_substring(const char *text, uint32_t length) {
    serial_tx_write((const uint8_t *)text, length);
}

// ── Board serial — WASI stdin/stdout for CLI mode ──
//
// The upstream multiplexer also checks board_serial_*() (weak symbols).
// We override them to provide WASI stdin/stdout as a fallback for CLI
// mode (Node.js, wasmtime without a JS wrapper).  In browser mode,
// board_serial_connected() returns false and these are never used.

void board_serial_early_init(void) {}
void board_serial_init(void) {}

bool board_serial_connected(void) {
    return port_mem.cli_mode;
}

uint32_t board_serial_bytes_available(void) {
    // In CLI mode, we can't poll WASI stdin without blocking.
    // Return 0 — the mp_hal_stdin_rx_chr loop will call
    // board_serial_read() directly when cli_mode is set.
    return 0;
}

char board_serial_read(void) {
    if (!port_mem.cli_mode) {
        return -1;
    }
    // Blocking read from WASI stdin
    unsigned char c;
    ssize_t ret = read(STDIN_FILENO, &c, 1);
    if (ret <= 0) {
        return 4;  // EOF -> Ctrl-D
    }
    if (c == '\n') {
        c = '\r';
    }
    return (char)c;
}

void board_serial_write_substring(const char *text, uint32_t length) {
    if (!port_mem.cli_mode) {
        return;
    }
    // Write to WASI stdout for CLI terminal display
    ssize_t ret = write(STDOUT_FILENO, text, length);
    (void)ret;
}
