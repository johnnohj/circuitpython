// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// DigitalInOut implementation for WASM port using message queue and yielding

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/Pull.h"
#include "shared-bindings/digitalio/DriveMode.h"
#include "shared-bindings/digitalio/Direction.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "supervisor/shared/tick.h"
#include "message_queue.h"

// Validate that an object is a valid pin
const mcu_pin_obj_t *common_hal_digitalio_validate_pin(mp_obj_t obj) {
    // In WASM, we accept pin numbers or pin objects
    // For now, just return a dummy pin - proper validation happens in construct
    return &pin_PA00;
}

// Construct a DigitalInOut object
digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self,
    const mcu_pin_obj_t *pin) {

    // Initialize the object
    self->pin = pin;
    self->input = true;  // Default to input
    self->open_drain = false;
    self->pull = PULL_NONE;

    // Send initialization request to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return DIGITALINOUT_PIN_BUSY;  // Queue full
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_GPIO_SET_DIRECTION;
    req->params.gpio_direction.pin = (uint8_t)((uintptr_t)pin);  // Pin number
    req->params.gpio_direction.direction = 0;  // Input

    message_queue_send_to_js(req_id);

    // Yield until JavaScript completes initialization
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    bool success = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    return success ? DIGITALINOUT_OK : DIGITALINOUT_PIN_BUSY;
}

// Deinitialize the pin
void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }
    // Mark as deinited
    self->pin = NULL;
}

// Check if deinited
bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

// Switch to input mode
digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self,
    digitalio_pull_t pull) {

    self->input = true;
    self->pull = pull;

    // Send direction change request
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return DIGITALINOUT_PIN_BUSY;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_GPIO_SET_DIRECTION;
    req->params.gpio_direction.pin = (uint8_t)((uintptr_t)self->pin);
    req->params.gpio_direction.direction = 0;  // Input

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    bool success = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    if (!success) {
        return DIGITALINOUT_PIN_BUSY;
    }

    // Set pull resistor if needed
    if (pull != PULL_NONE) {
        req_id = message_queue_alloc();
        if (req_id < 0) {
            return DIGITALINOUT_OK;  // Direction set, but pull failed
        }

        req = message_queue_get(req_id);
        req->type = MSG_TYPE_GPIO_SET_PULL;
        req->params.gpio_pull.pin = (uint8_t)((uintptr_t)self->pin);
        req->params.gpio_pull.pull = (pull == PULL_UP) ? 1 : 2;

        message_queue_send_to_js(req_id);
        WAIT_FOR_REQUEST_COMPLETION(req_id);
        message_queue_free(req_id);
    }

    return DIGITALINOUT_OK;
}

// Switch to output mode
digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self,
    bool value,
    digitalio_drive_mode_t drive_mode) {

    self->input = false;
    self->open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);

    // Set output value first
    common_hal_digitalio_digitalinout_set_value(self, value);

    // Then change direction
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return DIGITALINOUT_PIN_BUSY;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_GPIO_SET_DIRECTION;
    req->params.gpio_direction.pin = (uint8_t)((uintptr_t)self->pin);
    req->params.gpio_direction.direction = 1;  // Output

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    bool success = !message_queue_has_error(req_id);
    message_queue_free(req_id);

    return success ? DIGITALINOUT_OK : DIGITALINOUT_PIN_BUSY;
}

// Get direction
digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    return self->input ? DIRECTION_INPUT : DIRECTION_OUTPUT;
}

// Set pin value (output mode)
void common_hal_digitalio_digitalinout_set_value(
    digitalio_digitalinout_obj_t *self,
    bool value) {

    // THIS IS THE KEY FUNCTION - Shows the yielding pattern!

    // Allocate a message queue slot
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        // Queue full - could raise an exception or just return
        return;
    }

    // Set up the request
    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_GPIO_SET;
    req->params.gpio_set.pin = (uint8_t)((uintptr_t)self->pin);
    req->params.gpio_set.value = value ? 1 : 0;

    // Send to JavaScript (non-blocking)
    message_queue_send_to_js(req_id);

    // Yield until JavaScript completes the operation
    // This is where the magic happens - looks synchronous to Python,
    // but actually yields control to JavaScript via RUN_BACKGROUND_TASKS
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    // Check for errors (optional)
    if (message_queue_has_error(req_id)) {
        // Could raise an exception here
    }

    // Free the message queue slot
    message_queue_free(req_id);
}

// Get pin value (input mode)
bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    // Allocate a message queue slot
    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return false;  // Queue full
    }

    // Set up the request
    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_GPIO_GET;
    req->params.gpio_get.pin = (uint8_t)((uintptr_t)self->pin);

    // Send to JavaScript (non-blocking)
    message_queue_send_to_js(req_id);

    // Yield until JavaScript completes the read
    WAIT_FOR_REQUEST_COMPLETION(req_id);

    // Get the result
    bool value = req->response.gpio_value.value != 0;

    // Free the message queue slot
    message_queue_free(req_id);

    return value;
}

// Set drive mode
digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self,
    digitalio_drive_mode_t drive_mode) {

    self->open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);
    return DIGITALINOUT_OK;
}

// Get drive mode
digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {
    return self->open_drain ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

// Set pull resistor
digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self,
    digitalio_pull_t pull) {

    self->pull = pull;

    int32_t req_id = message_queue_alloc();
    if (req_id < 0) {
        return DIGITALINOUT_OK;
    }

    message_request_t *req = message_queue_get(req_id);
    req->type = MSG_TYPE_GPIO_SET_PULL;
    req->params.gpio_pull.pin = (uint8_t)((uintptr_t)self->pin);
    req->params.gpio_pull.pull = (pull == PULL_UP) ? 1 :
                                  (pull == PULL_DOWN) ? 2 : 0;

    message_queue_send_to_js(req_id);
    WAIT_FOR_REQUEST_COMPLETION(req_id);
    message_queue_free(req_id);

    return DIGITALINOUT_OK;
}

// Get pull resistor
digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {
    return self->pull;
}

// Never reset (no-op in WASM)
void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    // No-op for WASM
}

// Get register (not supported in WASM)
volatile uint32_t *common_hal_digitalio_digitalinout_get_reg(
    digitalio_digitalinout_obj_t *self,
    digitalinout_reg_op_t op,
    uint32_t *mask) {
    return NULL;
}

// Check if register operations are supported
bool common_hal_digitalio_has_reg_op(digitalinout_reg_op_t op) {
    return false;  // Not supported in WASM
}
