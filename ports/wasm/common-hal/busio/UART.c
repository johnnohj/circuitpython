// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// UART implementation for WASM port

#include "common-hal/busio/UART.h"
#include "shared-bindings/busio/UART.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stream.h"
#include <emscripten.h>
#include <string.h>

// Maximum number of UART ports
#define MAX_UART_PORTS 8

// UART buffer size
#define UART_BUFFER_SIZE 512

// UART port state
typedef struct {
    uint8_t tx_pin;
    uint8_t rx_pin;
    uint32_t baudrate;
    uint8_t bits;
    uint8_t parity;  // 0=none, 1=even, 2=odd
    uint8_t stop;
    bool enabled;
    bool never_reset;  // If true, don't reset this port during soft reset
    mp_float_t timeout;
    
    // RX ring buffer
    uint8_t rx_buffer[UART_BUFFER_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
    
    // TX ring buffer
    uint8_t tx_buffer[UART_BUFFER_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;
} uart_port_state_t;

// Global UART port state array
uart_port_state_t uart_ports[MAX_UART_PORTS];

EMSCRIPTEN_KEEPALIVE
uart_port_state_t* get_uart_state_ptr(void) {
    return uart_ports;
}

void busio_reset_uart_state(void) {
    for (int i = 0; i < MAX_UART_PORTS; i++) {
        // Skip ports marked as never_reset (e.g., used by supervisor console)
        if (uart_ports[i].never_reset) {
            continue;
        }

        uart_ports[i].tx_pin = 0xFF;
        uart_ports[i].rx_pin = 0xFF;
        uart_ports[i].baudrate = 9600;
        uart_ports[i].bits = 8;
        uart_ports[i].parity = 0;
        uart_ports[i].stop = 1;
        uart_ports[i].enabled = false;
        uart_ports[i].timeout = 1.0;
        uart_ports[i].rx_head = 0;
        uart_ports[i].rx_tail = 0;
        uart_ports[i].tx_head = 0;
        uart_ports[i].tx_tail = 0;
        memset(uart_ports[i].rx_buffer, 0, UART_BUFFER_SIZE);
        memset(uart_ports[i].tx_buffer, 0, UART_BUFFER_SIZE);
    }
}

// Find UART port by pin pair
static int8_t find_uart_port(uint8_t tx_pin, uint8_t rx_pin) {
    for (int i = 0; i < MAX_UART_PORTS; i++) {
        if (uart_ports[i].enabled &&
            uart_ports[i].tx_pin == tx_pin &&
            uart_ports[i].rx_pin == rx_pin) {
            return i;
        }
    }
    return -1;
}

// Find free UART port slot
static int8_t find_free_uart_port(void) {
    for (int i = 0; i < MAX_UART_PORTS; i++) {
        if (!uart_ports[i].enabled) {
            return i;
        }
    }
    return -1;
}

// Get number of bytes available in RX buffer
static uint32_t uart_rx_available(int8_t port_idx) {
    if (port_idx < 0) {
        return 0;
    }
    uint16_t head = uart_ports[port_idx].rx_head;
    uint16_t tail = uart_ports[port_idx].rx_tail;
    
    if (head >= tail) {
        return head - tail;
    } else {
        return UART_BUFFER_SIZE - tail + head;
    }
}

// Get number of free bytes in TX buffer
static uint32_t uart_tx_space(int8_t port_idx) {
    if (port_idx < 0) {
        return 0;
    }
    return UART_BUFFER_SIZE - 1 - uart_rx_available(port_idx);
}

void common_hal_busio_uart_construct(busio_uart_obj_t *self,
    const mcu_pin_obj_t *tx, const mcu_pin_obj_t *rx,
    const mcu_pin_obj_t *rts, const mcu_pin_obj_t *cts,
    const mcu_pin_obj_t *rs485_dir, bool rs485_invert,
    uint32_t baudrate, uint8_t bits, busio_uart_parity_t parity, uint8_t stop,
    mp_float_t timeout, uint16_t receiver_buffer_size, byte *receiver_buffer,
    bool sigint_enabled) {

    // Claim pins
    if (tx != NULL) {
        claim_pin(tx);
    }
    if (rx != NULL) {
        claim_pin(rx);
    }

    self->tx = tx;
    self->rx = rx;

    uint8_t tx_pin = (tx != NULL) ? tx->number : 0xFF;
    uint8_t rx_pin = (rx != NULL) ? rx->number : 0xFF;

    // Find or create UART port
    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    if (port_idx < 0) {
        port_idx = find_free_uart_port();
        if (port_idx < 0) {
            mp_raise_RuntimeError(MP_ERROR_TEXT("All UART ports in use"));
        }

        // Initialize new port
        uart_ports[port_idx].tx_pin = tx_pin;
        uart_ports[port_idx].rx_pin = rx_pin;
        uart_ports[port_idx].baudrate = baudrate;
        uart_ports[port_idx].bits = bits;
        uart_ports[port_idx].parity = parity;
        uart_ports[port_idx].stop = stop;
        uart_ports[port_idx].timeout = timeout;
        uart_ports[port_idx].enabled = true;
        uart_ports[port_idx].never_reset = false;
        uart_ports[port_idx].rx_head = 0;
        uart_ports[port_idx].rx_tail = 0;
        uart_ports[port_idx].tx_head = 0;
        uart_ports[port_idx].tx_tail = 0;
    }
}

void common_hal_busio_uart_deinit(busio_uart_obj_t *self) {
    if (common_hal_busio_uart_deinited(self)) {
        return;
    }

    uint8_t tx_pin = (self->tx != NULL) ? self->tx->number : 0xFF;
    uint8_t rx_pin = (self->rx != NULL) ? self->rx->number : 0xFF;

    // Find and disable port
    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    if (port_idx >= 0) {
        uart_ports[port_idx].enabled = false;
    }

    if (self->tx != NULL) {
        reset_pin_number(self->tx->number);
    }
    if (self->rx != NULL) {
        reset_pin_number(self->rx->number);
    }

    self->tx = NULL;
    self->rx = NULL;
}

bool common_hal_busio_uart_deinited(busio_uart_obj_t *self) {
    return self->tx == NULL && self->rx == NULL;
}

size_t common_hal_busio_uart_read(busio_uart_obj_t *self,
    uint8_t *data, size_t len, int *errcode) {

    if (self->rx == NULL) {
        *errcode = MP_EIO;
        return 0;
    }

    int8_t port_idx = find_uart_port(
        (self->tx != NULL) ? self->tx->number : 0xFF,
        self->rx->number
    );

    if (port_idx < 0) {
        *errcode = MP_EIO;
        return 0;
    }

    uint32_t available = uart_rx_available(port_idx);
    size_t to_read = (len < available) ? len : available;

    for (size_t i = 0; i < to_read; i++) {
        data[i] = uart_ports[port_idx].rx_buffer[uart_ports[port_idx].rx_tail];
        uart_ports[port_idx].rx_tail = (uart_ports[port_idx].rx_tail + 1) % UART_BUFFER_SIZE;
    }

    return to_read;
}

size_t common_hal_busio_uart_write(busio_uart_obj_t *self,
    const uint8_t *data, size_t len, int *errcode) {

    if (self->tx == NULL) {
        *errcode = MP_EIO;
        return 0;
    }

    int8_t port_idx = find_uart_port(
        self->tx->number,
        (self->rx != NULL) ? self->rx->number : 0xFF
    );

    if (port_idx < 0) {
        *errcode = MP_EIO;
        return 0;
    }

    uint32_t space = uart_tx_space(port_idx);
    size_t to_write = (len < space) ? len : space;

    for (size_t i = 0; i < to_write; i++) {
        uart_ports[port_idx].tx_buffer[uart_ports[port_idx].tx_head] = data[i];
        uart_ports[port_idx].tx_head = (uart_ports[port_idx].tx_head + 1) % UART_BUFFER_SIZE;
    }

    return to_write;
}

uint32_t common_hal_busio_uart_get_baudrate(busio_uart_obj_t *self) {
    return self->baudrate;
}

void common_hal_busio_uart_set_baudrate(busio_uart_obj_t *self, uint32_t baudrate) {
    self->baudrate = baudrate;

    uint8_t tx_pin = (self->tx != NULL) ? self->tx->number : 0xFF;
    uint8_t rx_pin = (self->rx != NULL) ? self->rx->number : 0xFF;

    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    if (port_idx >= 0) {
        uart_ports[port_idx].baudrate = baudrate;
    }
}

mp_float_t common_hal_busio_uart_get_timeout(busio_uart_obj_t *self) {
    return self->timeout_ms / 1000.0f;
}

void common_hal_busio_uart_set_timeout(busio_uart_obj_t *self, mp_float_t timeout) {
    self->timeout_ms = timeout * 1000;

    uint8_t tx_pin = (self->tx != NULL) ? self->tx->number : 0xFF;
    uint8_t rx_pin = (self->rx != NULL) ? self->rx->number : 0xFF;

    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    if (port_idx >= 0) {
        uart_ports[port_idx].timeout = timeout;
    }
}

uint32_t common_hal_busio_uart_rx_characters_available(busio_uart_obj_t *self) {
    if (self->rx == NULL) {
        return 0;
    }

    uint8_t tx_pin = (self->tx != NULL) ? self->tx->number : 0xFF;
    uint8_t rx_pin = self->rx->number;

    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    return uart_rx_available(port_idx);
}

void common_hal_busio_uart_clear_rx_buffer(busio_uart_obj_t *self) {
    if (self->rx == NULL) {
        return;
    }

    uint8_t tx_pin = (self->tx != NULL) ? self->tx->number : 0xFF;
    uint8_t rx_pin = self->rx->number;

    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    if (port_idx >= 0) {
        uart_ports[port_idx].rx_head = 0;
        uart_ports[port_idx].rx_tail = 0;
    }
}

bool common_hal_busio_uart_ready_to_tx(busio_uart_obj_t *self) {
    if (self->tx == NULL) {
        return false;
    }

    uint8_t tx_pin = self->tx->number;
    uint8_t rx_pin = (self->rx != NULL) ? self->rx->number : 0xFF;

    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    return (uart_tx_space(port_idx) > 0);
}

void common_hal_busio_uart_never_reset(busio_uart_obj_t *self) {
    // Mark this UART port as never_reset so it persists across soft resets
    // This is important for supervisor console and other system-managed UARTs
    uint8_t tx_pin = (self->tx != NULL) ? self->tx->number : 0xFF;
    uint8_t rx_pin = (self->rx != NULL) ? self->rx->number : 0xFF;

    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    if (port_idx >= 0) {
        uart_ports[port_idx].never_reset = true;

        // Also mark the pins as never_reset
        if (self->tx != NULL) {
            never_reset_pin_number(self->tx->number);
        }
        if (self->rx != NULL) {
            never_reset_pin_number(self->rx->number);
        }
    }
}

// Required for UART stream protocol
mp_uint_t common_hal_busio_uart_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    busio_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_uint_t ret;
    if (request == MP_STREAM_POLL) {
        uintptr_t flags = arg;
        ret = 0;
        if ((flags & MP_STREAM_POLL_RD) && common_hal_busio_uart_rx_characters_available(self) > 0) {
            ret |= MP_STREAM_POLL_RD;
        }
        if ((flags & MP_STREAM_POLL_WR) && common_hal_busio_uart_ready_to_tx(self)) {
            ret |= MP_STREAM_POLL_WR;
        }
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}
