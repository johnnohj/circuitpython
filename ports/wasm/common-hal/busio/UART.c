// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - UART using message queue with native yielding

#include "common-hal/busio/UART.h"
#include "shared-bindings/busio/UART.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "supervisor/shared/translate/translate.h"
#include "message_queue.h"

void common_hal_busio_uart_construct(busio_uart_obj_t *self,
    const mcu_pin_obj_t *tx, const mcu_pin_obj_t *rx,
    const mcu_pin_obj_t *rts, const mcu_pin_obj_t *cts,
    const mcu_pin_obj_t *rs485_dir, bool rs485_invert,
    uint32_t baudrate, uint8_t bits, busio_uart_parity_t parity, uint8_t stop,
    mp_float_t timeout, uint16_t receiver_buffer_size, byte *receiver_buffer,
    bool sigint_enabled) {

    self->tx = tx;
    self->rx = rx;
    self->baudrate = baudrate;
    self->character_bits = bits;
    self->timeout_ms = timeout * 1000;
    self->rx_ongoing = false;

    if (tx != NULL) {
        claim_pin(tx);
    }
    if (rx != NULL) {
        claim_pin(rx);
    }

    // Send UART initialization request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Message queue full"));
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_UART_INIT;
    req->params.uart_init.tx_pin = tx ? tx->number : 0xFF;
    req->params.uart_init.rx_pin = rx ? rx->number : 0xFF;
    req->params.uart_init.baudrate = baudrate;
    req->params.uart_init.bits = bits;
    req->params.uart_init.parity = parity;
    req->params.uart_init.stop = stop;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (message_queue_has_error(req_id)) {
        message_queue_free(req_id);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("UART init failed"));
    }

    message_queue_free(req_id);
}

void common_hal_busio_uart_deinit(busio_uart_obj_t *self) {
    if (common_hal_busio_uart_deinited(self)) {
        return;
    }

    // Send deinit request
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_UART_DEINIT;
        req->params.uart_deinit.tx_pin = self->tx ? self->tx->number : 0xFF;

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
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

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        *errcode = MP_EBUSY;
        return 0;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_UART_READ;
    req->params.uart_read.length = len;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    size_t bytes_read = 0;
    if (!message_queue_has_error(req_id)) {
        bytes_read = req->response.uart_data.length;
        if (bytes_read > len) {
            bytes_read = len;
        }
        memcpy(data, req->response.uart_data.data, bytes_read);
        *errcode = 0;
    } else {
        *errcode = MP_EIO;
    }

    message_queue_free(req_id);
    return bytes_read;
}

size_t common_hal_busio_uart_write(busio_uart_obj_t *self,
    const uint8_t *data, size_t len, int *errcode) {

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        *errcode = MP_EBUSY;
        return 0;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_UART_WRITE;
    req->params.uart_write.length = len;

    // Copy data to request payload (up to max size)
    size_t copy_len = len < MESSAGE_QUEUE_MAX_PAYLOAD ? len : MESSAGE_QUEUE_MAX_PAYLOAD;
    memcpy(req->params.uart_write.data, data, copy_len);

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    size_t bytes_written = copy_len;
    if (message_queue_has_error(req_id)) {
        *errcode = MP_EIO;
        bytes_written = 0;
    } else {
        *errcode = 0;
    }

    message_queue_free(req_id);
    return bytes_written;
}

uint32_t common_hal_busio_uart_get_baudrate(busio_uart_obj_t *self) {
    return self->baudrate;
}

void common_hal_busio_uart_set_baudrate(busio_uart_obj_t *self, uint32_t baudrate) {
    self->baudrate = baudrate;

    // Send baudrate update to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_UART_SET_BAUDRATE;
        req->params.uart_set_baudrate.baudrate = baudrate;

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
    }
}

mp_float_t common_hal_busio_uart_get_timeout(busio_uart_obj_t *self) {
    return (mp_float_t)(self->timeout_ms / 1000.0f);
}

void common_hal_busio_uart_set_timeout(busio_uart_obj_t *self, mp_float_t timeout) {
    self->timeout_ms = timeout * 1000;
}

uint32_t common_hal_busio_uart_rx_characters_available(busio_uart_obj_t *self) {
    // Query JavaScript for available characters
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return 0;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_UART_RX_AVAILABLE;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    uint32_t available = req->response.uart_available.count;
    message_queue_free(req_id);

    return available;
}

void common_hal_busio_uart_clear_rx_buffer(busio_uart_obj_t *self) {
    // Send clear buffer request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_UART_CLEAR_RX;

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
    }
}

bool common_hal_busio_uart_ready_to_tx(busio_uart_obj_t *self) {
    // For WASM, we're always ready (buffered)
    return true;
}

void common_hal_busio_uart_never_reset(busio_uart_obj_t *self) {
    if (self->tx != NULL) {
        never_reset_pin_number(self->tx->number);
    }
    if (self->rx != NULL) {
        never_reset_pin_number(self->rx->number);
    }
}
