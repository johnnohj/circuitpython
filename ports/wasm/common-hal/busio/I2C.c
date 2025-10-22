// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - I2C using message queue with native yielding

#include "common-hal/busio/I2C.h"
#include "shared-bindings/busio/I2C.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "supervisor/shared/translate/translate.h"
#include "message_queue.h"

void common_hal_busio_i2c_construct(busio_i2c_obj_t *self,
    const mcu_pin_obj_t *scl, const mcu_pin_obj_t *sda, uint32_t frequency, uint32_t timeout) {

    self->scl = scl;
    self->sda = sda;
    self->has_lock = false;

    claim_pin(scl);
    claim_pin(sda);

    // Send I2C initialization request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Message queue full"));
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_I2C_INIT;
    req->params.i2c_init.scl_pin = (uint8_t)((uintptr_t)scl);
    req->params.i2c_init.sda_pin = (uint8_t)((uintptr_t)sda);
    req->params.i2c_init.frequency = frequency;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (message_queue_has_error(req_id)) {
        message_queue_free(req_id);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("I2C init failed"));
    }

    message_queue_free(req_id);
}

void common_hal_busio_i2c_deinit(busio_i2c_obj_t *self) {
    if (common_hal_busio_i2c_deinited(self)) {
        return;
    }

    // Send deinit request
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_I2C_DEINIT;
        req->params.i2c_deinit.scl_pin = (uint8_t)((uintptr_t)self->scl);

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
    }

    reset_pin_number(self->scl->number);
    reset_pin_number(self->sda->number);
    self->scl = NULL;
    self->sda = NULL;
}

bool common_hal_busio_i2c_deinited(busio_i2c_obj_t *self) {
    return self->sda == NULL;
}

void common_hal_busio_i2c_mark_deinit(busio_i2c_obj_t *self) {
    self->sda = NULL;
    self->scl = NULL;
}

bool common_hal_busio_i2c_try_lock(busio_i2c_obj_t *self) {
    bool grabbed_lock = false;
    if (!self->has_lock) {
        grabbed_lock = true;
        self->has_lock = true;
    }
    return grabbed_lock;
}

bool common_hal_busio_i2c_has_lock(busio_i2c_obj_t *self) {
    return self->has_lock;
}

void common_hal_busio_i2c_unlock(busio_i2c_obj_t *self) {
    self->has_lock = false;
}

bool common_hal_busio_i2c_probe(busio_i2c_obj_t *self, uint8_t addr) {
    // Allocate message queue slot
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return false;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_I2C_PROBE;
    req->params.i2c_probe.address = addr;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    bool found = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    return found;
}

uint8_t common_hal_busio_i2c_write(busio_i2c_obj_t *self, uint16_t addr,
    const uint8_t *data, size_t len) {

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return MP_EBUSY;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_I2C_WRITE;
    req->params.i2c_write.address = addr;
    req->params.i2c_write.length = len;

    // Copy data to request payload (up to max size)
    size_t copy_len = len < MESSAGE_QUEUE_MAX_PAYLOAD ? len : MESSAGE_QUEUE_MAX_PAYLOAD;
    memcpy(req->params.i2c_write.data, data, copy_len);

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    uint8_t status = message_queue_has_error(req_id) ? MP_EIO : 0;
    message_queue_free(req_id);

    return status;
}

uint8_t common_hal_busio_i2c_read(busio_i2c_obj_t *self, uint16_t addr,
    uint8_t *data, size_t len) {

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return MP_EBUSY;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_I2C_READ;
    req->params.i2c_read.address = addr;
    req->params.i2c_read.length = len;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (!message_queue_has_error(req_id)) {
        // Copy response data
        size_t copy_len = len < MESSAGE_QUEUE_MAX_PAYLOAD ? len : MESSAGE_QUEUE_MAX_PAYLOAD;
        memcpy(data, req->response.data, copy_len);
    }

    uint8_t status = message_queue_has_error(req_id) ? MP_EIO : 0;
    message_queue_free(req_id);

    return status;
}

uint8_t common_hal_busio_i2c_write_read(busio_i2c_obj_t *self, uint16_t addr,
    uint8_t *out_data, size_t out_len, uint8_t *in_data, size_t in_len) {

    // First write
    uint8_t status = common_hal_busio_i2c_write(self, addr, out_data, out_len);
    if (status != 0) {
        return status;
    }

    // Then read
    return common_hal_busio_i2c_read(self, addr, in_data, in_len);
}

void common_hal_busio_i2c_never_reset(busio_i2c_obj_t *self) {
    never_reset_pin_number(self->scl->number);
    never_reset_pin_number(self->sda->number);
}
