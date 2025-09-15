// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// JavaScript-backed AnalogIn object
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    int js_analogin_ref;  // Reference to JavaScript AnalogIn object
    uint16_t reference_voltage_mv;
} analogio_analogin_obj_t;

// JavaScript semihosting functions (implemented in common-hal/analogio/AnalogIn.c)
extern bool js_analogin_create(const mcu_pin_obj_t *pin, int *js_ref_out);
extern void js_analogin_deinit(int js_ref);
extern uint16_t js_analogin_get_value(int js_ref);
extern float js_analogin_get_reference_voltage(int js_ref);