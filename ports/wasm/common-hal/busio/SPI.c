// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - SPI using message queue with native yielding

#include "common-hal/busio/SPI.h"
#include "shared-bindings/busio/SPI.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "supervisor/shared/translate/translate.h"
#include "message_queue.h"

void common_hal_busio_spi_construct(busio_spi_obj_t *self,
    const mcu_pin_obj_t *clock, const mcu_pin_obj_t *mosi,
    const mcu_pin_obj_t *miso, bool half_duplex) {

    self->clock = clock;
    self->MOSI = mosi;
    self->MISO = miso;
    self->has_lock = false;

    claim_pin(clock);
    if (mosi != NULL) {
        claim_pin(mosi);
    }
    if (miso != NULL) {
        claim_pin(miso);
    }

    // Send SPI initialization request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Message queue full"));
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_SPI_INIT;
    req->params.spi_init.clock_pin = (uint8_t)((uintptr_t)clock);
    req->params.spi_init.mosi_pin = mosi ? (uint8_t)((uintptr_t)mosi) : 0xFF;
    req->params.spi_init.miso_pin = miso ? (uint8_t)((uintptr_t)miso) : 0xFF;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (message_queue_has_error(req_id)) {
        message_queue_free(req_id);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("SPI init failed"));
    }

    message_queue_free(req_id);
}

void common_hal_busio_spi_deinit(busio_spi_obj_t *self) {
    if (common_hal_busio_spi_deinited(self)) {
        return;
    }

    // Send deinit request
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_SPI_DEINIT;
        req->params.spi_deinit.clock_pin = (uint8_t)((uintptr_t)self->clock);

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
    }

    reset_pin_number(self->clock->number);
    if (self->MOSI != NULL) {
        reset_pin_number(self->MOSI->number);
    }
    if (self->MISO != NULL) {
        reset_pin_number(self->MISO->number);
    }
    self->clock = NULL;
    self->MOSI = NULL;
    self->MISO = NULL;
}

bool common_hal_busio_spi_deinited(busio_spi_obj_t *self) {
    return self->clock == NULL;
}

bool common_hal_busio_spi_configure(busio_spi_obj_t *self,
    uint32_t baudrate, uint8_t polarity, uint8_t phase, uint8_t bits) {

    self->baudrate = baudrate;
    self->polarity = polarity;
    self->phase = phase;
    self->bits = bits;

    // Send configure request
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return false;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_SPI_CONFIGURE;
    req->params.spi_configure.baudrate = baudrate;
    req->params.spi_configure.polarity = polarity;
    req->params.spi_configure.phase = phase;
    req->params.spi_configure.bits = bits;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    bool success = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    return success;
}

bool common_hal_busio_spi_try_lock(busio_spi_obj_t *self) {
    bool grabbed_lock = false;
    if (!self->has_lock) {
        grabbed_lock = true;
        self->has_lock = true;
    }
    return grabbed_lock;
}

bool common_hal_busio_spi_has_lock(busio_spi_obj_t *self) {
    return self->has_lock;
}

void common_hal_busio_spi_unlock(busio_spi_obj_t *self) {
    self->has_lock = false;
}

bool common_hal_busio_spi_write(busio_spi_obj_t *self,
    const uint8_t *data, size_t len) {

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return false;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_SPI_WRITE;
    req->params.spi_write.length = len;

    // Copy data to request payload (up to max size)
    size_t copy_len = len < MESSAGE_QUEUE_MAX_PAYLOAD ? len : MESSAGE_QUEUE_MAX_PAYLOAD;
    memcpy(req->params.spi_write.data, data, copy_len);

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    bool success = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    return success;
}

bool common_hal_busio_spi_read(busio_spi_obj_t *self,
    uint8_t *data, size_t len, uint8_t write_value) {

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return false;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_SPI_READ;
    req->params.spi_read.length = len;
    req->params.spi_read.write_value = write_value;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (!message_queue_has_error(req_id)) {
        // Copy response data
        size_t copy_len = len < MESSAGE_QUEUE_MAX_PAYLOAD ? len : MESSAGE_QUEUE_MAX_PAYLOAD;
        memcpy(data, req->response.data, copy_len);
    }

    bool success = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    return success;
}

bool common_hal_busio_spi_transfer(busio_spi_obj_t *self,
    const uint8_t *data_out, uint8_t *data_in, size_t len) {

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return false;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_SPI_TRANSFER;
    req->params.spi_transfer.length = len;

    // Copy output data to request payload
    size_t copy_len = len < MESSAGE_QUEUE_MAX_PAYLOAD ? len : MESSAGE_QUEUE_MAX_PAYLOAD;
    memcpy(req->params.spi_transfer.data_out, data_out, copy_len);

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (!message_queue_has_error(req_id)) {
        // Copy response data
        memcpy(data_in, req->response.data, copy_len);
    }

    bool success = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    return success;
}

void common_hal_busio_spi_never_reset(busio_spi_obj_t *self) {
    never_reset_pin_number(self->clock->number);
    if (self->MOSI != NULL) {
        never_reset_pin_number(self->MOSI->number);
    }
    if (self->MISO != NULL) {
        never_reset_pin_number(self->MISO->number);
    }
}
