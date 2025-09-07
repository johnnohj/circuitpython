// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// JavaScript-backed AnalogOut object
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    int js_analogout_ref;  // Reference to JavaScript AnalogOut object
    uint16_t value;
} analogio_analogout_obj_t;

// JavaScript semihosting functions
extern bool js_analogout_create(const mcu_pin_obj_t *pin, int *js_ref_out);
extern void js_analogout_deinit(int js_ref);
extern void js_analogout_set_value(int js_ref, uint16_t value);