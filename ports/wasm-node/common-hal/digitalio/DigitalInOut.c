// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "hal_provider.h"

// DigitalInOut implementation using HAL provider pattern

digitalinout_result_t common_hal_digitalio_digitalinout_construct(digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {
    const hal_provider_t *provider = hal_get_provider();
    if (provider == NULL) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("No HAL provider available"));
    }

    // Create pin name from pin number for HAL provider
    char pin_name[16];
    snprintf(pin_name, sizeof(pin_name), "GP%d", pin->number);

    // Find or create HAL pin for this MCU pin
    hal_pin_t *hal_pin = hal_pin_find_by_number(pin->number);
    if (hal_pin == NULL) {
        hal_pin = hal_pin_create(pin->number, pin_name, HAL_CAP_DIGITAL_IO);
    }

    if (hal_pin == NULL || !hal_pin_supports_digital(hal_pin)) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("Pin does not support digital I/O"));
    }

    // Initialize the DigitalInOut object
    self->pin = pin;
    self->output = false;
    self->open_drain = false;
    self->pull = PULL_NONE;
    self->value = false;

    // For compatibility with existing code structure
    self->js_digitalinout_ref = (int)hal_pin; // Store HAL pin reference

    // Claim the pin
    claim_pin(pin);

    return DIGITALINOUT_OK;
}

bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }

    hal_pin_t *hal_pin = (hal_pin_t*)self->js_digitalinout_ref;
    if (hal_pin != NULL && hal_pin->provider && hal_pin->provider->pin_ops) {
        hal_pin->provider->pin_ops->pin_deinit(hal_pin);
    }

    self->pin = NULL;
    self->js_digitalinout_ref = -1;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    hal_pin_t *hal_pin = (hal_pin_t*)self->js_digitalinout_ref;
    if (hal_pin == NULL || hal_pin->provider == NULL || hal_pin->provider->pin_ops == NULL) {
        return DIGITALINOUT_OK;
    }

    const hal_pin_ops_t *ops = hal_pin->provider->pin_ops;

    // Set direction to input
    ops->digital_set_direction(hal_pin, false);

    // Set pull resistor
    int pull_mode = (pull == PULL_UP) ? 1 : (pull == PULL_DOWN) ? 2 : 0;
    ops->digital_set_pull(hal_pin, pull_mode);

    self->output = false;
    self->pull = pull;

    return DIGITALINOUT_OK;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(digitalio_digitalinout_obj_t *self, bool value, digitalio_drive_mode_t drive_mode) {
    hal_pin_t *hal_pin = (hal_pin_t*)self->js_digitalinout_ref;
    if (hal_pin == NULL || hal_pin->provider == NULL || hal_pin->provider->pin_ops == NULL) {
        return DIGITALINOUT_OK;
    }

    const hal_pin_ops_t *ops = hal_pin->provider->pin_ops;

    // Set direction to output
    ops->digital_set_direction(hal_pin, true);

    // Set initial value
    ops->digital_set_value(hal_pin, value);

    self->output = true;
    self->value = value;
    self->open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);

    return DIGITALINOUT_OK;
}

digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(digitalio_digitalinout_obj_t *self) {
    return self->output ? DIRECTION_OUTPUT : DIRECTION_INPUT;
}

void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self, bool value) {
    hal_pin_t *hal_pin = (hal_pin_t*)self->js_digitalinout_ref;
    if (hal_pin == NULL || hal_pin->provider == NULL || hal_pin->provider->pin_ops == NULL) {
        return;
    }

    if (self->output) {
        hal_pin->provider->pin_ops->digital_set_value(hal_pin, value);
        self->value = value;
    }
}

bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    hal_pin_t *hal_pin = (hal_pin_t*)self->js_digitalinout_ref;
    if (hal_pin == NULL || hal_pin->provider == NULL || hal_pin->provider->pin_ops == NULL) {
        return false;
    }

    if (self->output) {
        return self->value;
    } else {
        return hal_pin->provider->pin_ops->digital_get_value(hal_pin);
    }
}

digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(digitalio_digitalinout_obj_t *self) {
    return self->pull;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    if (self->output) {
        mp_raise_AttributeError(MP_ERROR_TEXT("Cannot set pull on output pin"));
    }

    hal_pin_t *hal_pin = (hal_pin_t*)self->js_digitalinout_ref;
    if (hal_pin == NULL || hal_pin->provider == NULL || hal_pin->provider->pin_ops == NULL) {
        return DIGITALINOUT_OK;
    }

    int pull_mode = (pull == PULL_UP) ? 1 : (pull == PULL_DOWN) ? 2 : 0;
    hal_pin->provider->pin_ops->digital_set_pull(hal_pin, pull_mode);

    self->pull = pull;

    return DIGITALINOUT_OK;
}

digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(digitalio_digitalinout_obj_t *self) {
    return self->open_drain ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(digitalio_digitalinout_obj_t *self, digitalio_drive_mode_t drive_mode) {
    if (!self->output) {
        mp_raise_AttributeError(MP_ERROR_TEXT("Cannot set drive mode on input pin"));
    }

    self->open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);
    // Note: In a full implementation, this would configure the pin's output driver

    return DIGITALINOUT_OK;
}