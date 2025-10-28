// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// AnalogOut implementation for WASM port
// Uses direct state array access (analog_state from AnalogIn.h)

#include "common-hal/analogio/AnalogOut.h"
#include "common-hal/analogio/AnalogIn.h"  // For analog_state array
#include "shared-bindings/analogio/AnalogOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "supervisor/shared/translate/translate.h"

void common_hal_analogio_analogout_construct(analogio_analogout_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    // Initialize analog state for DAC output
    uint8_t pin_num = pin->number;
    analog_state[pin_num].is_output = true;  // DAC output
    analog_state[pin_num].enabled = true;
    analog_state[pin_num].value = 0;  // Start at 0V
}

void common_hal_analogio_analogout_deinit(analogio_analogout_obj_t *self) {
    if (common_hal_analogio_analogout_deinited(self)) {
        return;
    }

    // Disable analog pin
    analog_state[self->pin->number].enabled = false;

    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogout_deinited(analogio_analogout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_analogio_analogout_set_value(analogio_analogout_obj_t *self, uint16_t value) {
    // Write directly to analog state array
    // JavaScript can read this to visualize DAC output
    analog_state[self->pin->number].value = value;
}

void common_hal_analogio_analogout_never_reset(analogio_analogout_obj_t *self) {
    // No-op for WASM
}
