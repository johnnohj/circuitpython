// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - AnalogIn using message queue with native yielding

#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogIn.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "supervisor/shared/translate/translate.h"
#include "message_queue.h"

void common_hal_analogio_analogin_construct(analogio_analogin_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    // Send initialization request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Message queue full"));
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_ANALOG_INIT;
    req->params.analog_init.pin = (uint8_t)((uintptr_t)pin);
    req->params.analog_init.is_output = false;

    message_queue_send_to_js(req_id);

    // Yield until JavaScript completes initialization
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (message_queue_has_error(req_id)) {
        message_queue_free(req_id);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("AnalogIn init failed"));
    }

    message_queue_free(req_id);
}

void common_hal_analogio_analogin_deinit(analogio_analogin_obj_t *self) {
    if (common_hal_analogio_analogin_deinited(self)) {
        return;
    }

    // Send deinit request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_ANALOG_DEINIT;
        req->params.analog_deinit.pin = (uint8_t)((uintptr_t)self->pin);

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
    }

    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogin_deinited(analogio_analogin_obj_t *self) {
    return self->pin == NULL;
}

uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    // Allocate message queue slot
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return 0;  // Queue full
    }

    // Set up request
    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_ANALOG_READ;
    req->params.analog_read.pin = (uint8_t)((uintptr_t)self->pin);

    // Send to JavaScript (non-blocking)
    message_queue_send_to_js(req_id);

    // Yield until complete (THE MAGIC!)
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    // Get response
    uint16_t value = req->response.analog_value.value;

    // Free slot
    message_queue_free(req_id);

    return value;
}

float common_hal_analogio_analogin_get_reference_voltage(analogio_analogin_obj_t *self) {
    // Standard reference voltage for most systems
    return 3.3f;
}
