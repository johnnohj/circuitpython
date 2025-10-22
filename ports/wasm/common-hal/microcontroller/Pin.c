// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - virtual pins for JavaScript simulation

#include "py/runtime.h"

#include "shared-bindings/microcontroller/Pin.h"

void reset_all_pins(void) {
}

void never_reset_pin_number(uint8_t pin_number) {
}

void reset_pin_number(uint8_t pin_number) {
}

void common_hal_never_reset_pin(const mcu_pin_obj_t *pin) {
    never_reset_pin_number(pin->number);
}

void common_hal_reset_pin(const mcu_pin_obj_t *pin) {
    reset_pin_number(pin->number);
}

void claim_pin(const mcu_pin_obj_t *pin) {
}

bool pin_number_is_free(uint8_t pin_number) {
    return true;
}

bool common_hal_mcu_pin_is_free(const mcu_pin_obj_t *pin) {
    return pin_number_is_free(pin->number);
}

uint8_t common_hal_mcu_pin_number(const mcu_pin_obj_t *pin) {
    return pin->number;
}

void common_hal_mcu_pin_claim(const mcu_pin_obj_t *pin) {
    return claim_pin(pin);
}

void common_hal_mcu_pin_reset_number(uint8_t pin_no) {
    reset_pin_number(pin_no);
}

// This macro is used to simplify pin definition
#define PIN(p_number) \
    const mcu_pin_obj_t pin_GPIO##p_number = { \
        { &mcu_pin_type }, \
        .number = p_number \
    }

// Virtual pin for WASM - just a dummy pin
const mcu_pin_obj_t pin_PA00 = {
    { &mcu_pin_type },
    .number = 0
};
