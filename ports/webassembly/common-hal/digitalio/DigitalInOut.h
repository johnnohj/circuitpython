// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "shared-bindings/digitalio/Direction.h"
#include "shared-bindings/digitalio/DriveMode.h"
#include "shared-bindings/digitalio/Pull.h"
#include "py/obj.h"

// JavaScript-backed DigitalInOut object
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    int js_digitalinout_ref;  // Reference to JavaScript DigitalInOut object
    bool output;
    bool open_drain;
    digitalio_pull_t pull;
    bool value;
} digitalio_digitalinout_obj_t;

// JavaScript semihosting functions (implemented in proxy_c.c)
extern bool js_digitalio_create(const mcu_pin_obj_t *pin, int *js_ref_out);
extern void js_digitalio_deinit(int js_ref);
extern void js_digitalio_switch_to_input(int js_ref, digitalio_pull_t pull);
extern void js_digitalio_switch_to_output(int js_ref, bool value, digitalio_drive_mode_t drive_mode);
extern bool js_digitalio_get_value(int js_ref);
extern void js_digitalio_set_value(int js_ref, bool value);
extern digitalio_direction_t js_digitalio_get_direction(int js_ref);
extern digitalio_pull_t js_digitalio_get_pull(int js_ref);
extern void js_digitalio_set_pull(int js_ref, digitalio_pull_t pull);
extern digitalio_drive_mode_t js_digitalio_get_drive_mode(int js_ref);
extern void js_digitalio_set_drive_mode(int js_ref, digitalio_drive_mode_t drive_mode);