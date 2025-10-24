// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// DigitalInOut implementation for WASM port using virtual hardware

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/Pull.h"
#include "shared-bindings/digitalio/DriveMode.h"
#include "shared-bindings/digitalio/Direction.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "virtual_hardware.h"

// Validate that an object is a valid pin
const mcu_pin_obj_t *common_hal_digitalio_validate_pin(mp_obj_t obj) {
    // In WASM, we accept pin numbers or pin objects
    // For now, just return a dummy pin - proper validation happens in construct
    return &pin_GPIO0;
}

// Construct a DigitalInOut object
digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self,
    const mcu_pin_obj_t *pin) {

    // Initialize the object - only store which pin
    self->pin = pin;

    // Initialize virtual hardware to input mode (default)
    virtual_gpio_set_direction(pin->number, 0);  // 0 = input
    virtual_gpio_set_pull(pin->number, 0);       // 0 = PULL_NONE
    virtual_gpio_set_open_drain(pin->number, false);  // Push-pull by default

    return DIGITALINOUT_OK;
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

    // Update virtual hardware - single source of truth
    virtual_gpio_set_direction(self->pin->number, 0);  // 0 = input

    // Set pull resistor
    uint8_t pull_mode = 0;  // PULL_NONE
    if (pull == PULL_UP) {
        pull_mode = 1;
    } else if (pull == PULL_DOWN) {
        pull_mode = 2;
    }
    virtual_gpio_set_pull(self->pin->number, pull_mode);

    return DIGITALINOUT_OK;
}

// Switch to output mode
digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self,
    bool value,
    digitalio_drive_mode_t drive_mode) {

    // Update virtual hardware - single source of truth
    virtual_gpio_set_direction(self->pin->number, 1);  // 1 = output
    virtual_gpio_set_value(self->pin->number, value);
    virtual_gpio_set_open_drain(self->pin->number, drive_mode == DRIVE_MODE_OPEN_DRAIN);

    return DIGITALINOUT_OK;
}

// Get direction
digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    // Read from virtual hardware
    uint8_t dir = virtual_gpio_get_direction(self->pin->number);
    return (dir == 0) ? DIRECTION_INPUT : DIRECTION_OUTPUT;
}

// Set pin value (output mode)
void common_hal_digitalio_digitalinout_set_value(
    digitalio_digitalinout_obj_t *self,
    bool value) {

    // Update virtual hardware directly - no message queue needed!
    virtual_gpio_set_value(self->pin->number, value);
}

// Get pin value
bool common_hal_digitalio_digitalinout_get_value(
    digitalio_digitalinout_obj_t *self) {

    // Read from virtual hardware directly
    return virtual_gpio_get_value(self->pin->number);
}

// Set drive mode
digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self,
    digitalio_drive_mode_t drive_mode) {

    // Update virtual hardware
    virtual_gpio_set_open_drain(self->pin->number, drive_mode == DRIVE_MODE_OPEN_DRAIN);
    return DIGITALINOUT_OK;
}

// Get drive mode
digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {
    // Read from virtual hardware
    bool open_drain = virtual_gpio_get_open_drain(self->pin->number);
    return open_drain ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

// Set pull resistor
digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self,
    digitalio_pull_t pull) {

    // Update virtual hardware
    uint8_t pull_mode = 0;  // PULL_NONE
    if (pull == PULL_UP) {
        pull_mode = 1;
    } else if (pull == PULL_DOWN) {
        pull_mode = 2;
    }
    virtual_gpio_set_pull(self->pin->number, pull_mode);

    return DIGITALINOUT_OK;
}

// Get pull resistor
digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {
    // Read from virtual hardware
    uint8_t pull = virtual_gpio_get_pull(self->pin->number);
    if (pull == 1) return PULL_UP;
    if (pull == 2) return PULL_DOWN;
    return PULL_NONE;
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
