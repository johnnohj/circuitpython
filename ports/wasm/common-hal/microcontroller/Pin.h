// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - virtual pins for JavaScript simulation

#pragma once

#include <assert.h>
#include <stdint.h>

#include <py/obj.h>

typedef struct {
    mp_obj_base_t base;
    uint8_t number;
} mcu_pin_obj_t;

extern const mcu_pin_obj_t pin_PA00;

#define NO_PIN (&pin_PA00)

void reset_all_pins(void);
void reset_pin_number(uint8_t pin_number);
void never_reset_pin_number(uint8_t pin_number);
void claim_pin(const mcu_pin_obj_t *pin);
bool pin_number_is_free(uint8_t pin_number);
