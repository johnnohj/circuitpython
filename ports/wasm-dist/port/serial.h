// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/serial.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/serial.h — Serial ring buffer helpers.
//
// Read/write serial data through the MEMFS-backed ring buffers in
// port_mem.  JS writes to /hal/serial/rx (keyboard input), C reads.
// C writes to /hal/serial/tx (output), JS reads.
//
// These are the low-level ring operations.  The supervisor serial
// layer (Phase 4.5) provides the higher-level routing: display path
// (terminalio), console path (stdout), and input multiplexing.
//
// Design refs:
//   design/behavior/02-serial-and-stack.md  (serial architecture)
//   design/wasm-layer.md                    (wasm layer, ring buffers)

#ifndef PORT_SERIAL_H
#define PORT_SERIAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Write data into the TX ring (C→JS output).
// Returns number of bytes actually written (may be < len if ring full).
size_t serial_tx_write(const uint8_t *data, size_t len);

// Write data into the RX ring (JS→C input, from C side).
// Used by cp_serial_push() WASM export.
size_t serial_rx_write(const uint8_t *data, size_t len);

// Read data from the RX ring (JS→C input).
// Returns number of bytes actually read (may be < max_len if ring empty).
size_t serial_rx_read(uint8_t *buf, size_t max_len);

// Check how many bytes are available to read from RX.
size_t serial_rx_available(void);

// Check if TX ring has space for len bytes.
bool serial_tx_has_space(size_t len);

// Write a C string to TX (convenience wrapper).
void serial_tx_print(const char *str);

#endif // PORT_SERIAL_H
