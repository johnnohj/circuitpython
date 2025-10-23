// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - AnalogOut using message queue with native yielding

#include "common-hal/analogio/AnalogOut.h"
#include "shared-bindings/analogio/AnalogOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "supervisor/shared/translate/translate.h"
#include "message_queue.h"

void common_hal_analogio_analogout_construct(analogio_analogout_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    // Send initialization request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Message queue full"));
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_ANALOG_INIT;
    req->params.analog_init.pin = pin->number;
    req->params.analog_init.is_output = true;

    message_queue_send_to_js(req_id);

    // Yield until JavaScript completes initialization
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    if (message_queue_has_error(req_id)) {
        message_queue_free(req_id);
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("AnalogOut init failed"));
    }

    message_queue_free(req_id);
}

void common_hal_analogio_analogout_deinit(analogio_analogout_obj_t *self) {
    if (common_hal_analogio_analogout_deinited(self)) {
        return;
    }

    // Send deinit request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_ANALOG_DEINIT;
        req->params.analog_deinit.pin = self->pin->number;

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
    }

    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogout_deinited(analogio_analogout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_analogio_analogout_set_value(analogio_analogout_obj_t *self, uint16_t value) {
    // Allocate message queue slot
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return;  // Queue full
    }

    // Set up request
    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_ANALOG_WRITE;
    req->params.analog_write.pin = self->pin->number;
    req->params.analog_write.value = value;

    // Send to JavaScript (non-blocking)
    message_queue_send_to_js(req_id);

    // Yield until complete
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    // Free slot
    message_queue_free(req_id);
}
