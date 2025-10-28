// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// AnalogIn implementation for WASM port
// This is common-hal - the hardware abstraction layer
// For WASM, "hardware" = in-memory state arrays accessible to JavaScript

#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogIn.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "supervisor/shared/translate/translate.h"
#include <emscripten.h>
#include <string.h>

// Analog state array - this is the "virtual hardware" for the WASM port
// JavaScript accesses this via library.js to write simulated ADC values
analog_pin_state_t analog_state[64];

// Expose pointer to JavaScript via library.js
EMSCRIPTEN_KEEPALIVE
analog_pin_state_t* get_analog_state_ptr(void) {
    return analog_state;
}

// Initialize analog state (called during port initialization)
void analogio_reset_analog_state(void) {
    for (int i = 0; i < 64; i++) {
        analog_state[i].value = 32768;  // Mid-range default
        analog_state[i].is_output = false;
        analog_state[i].enabled = false;
    }
}

const mcu_pin_obj_t *common_hal_analogio_analogin_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);
}

void common_hal_analogio_analogin_construct(analogio_analogin_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    // Initialize analog state
    uint8_t pin_num = pin->number;
    analog_state[pin_num].is_output = false;  // ADC input
    analog_state[pin_num].enabled = true;
    analog_state[pin_num].value = 32768;  // Mid-range default
}

void common_hal_analogio_analogin_deinit(analogio_analogin_obj_t *self) {
    if (common_hal_analogio_analogin_deinited(self)) {
        return;
    }

    // Disable analog pin
    analog_state[self->pin->number].enabled = false;

    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogin_deinited(analogio_analogin_obj_t *self) {
    return self->pin == NULL;
}

uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    // Read directly from analog state array
    // JavaScript can write to this array to simulate sensor readings
    return analog_state[self->pin->number].value;
}

float common_hal_analogio_analogin_get_reference_voltage(analogio_analogin_obj_t *self) {
    // Standard reference voltage for most systems
    return 3.3f;
}
