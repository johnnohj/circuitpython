// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// GPIO state for WASM virtual hardware
// This is the SINGLE SOURCE OF TRUTH for GPIO state in the WASM port
typedef struct {
    bool value;          // Current pin value
    uint8_t direction;   // 0=input, 1=output
    uint8_t pull;        // 0=none, 1=up, 2=down
    bool open_drain;     // Open-drain output mode
    bool enabled;        // Pin is enabled/in-use
    bool never_reset;    // If true, don't reset during soft reset
} gpio_pin_state_t;

// 64 virtual GPIO pins - exposed to JavaScript via library.js
extern gpio_pin_state_t gpio_state[64];

// DigitalInOut HAL object
// Only stores which pin this object controls
// Actual pin state is in gpio_state[] array
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;  // Which pin this object controls
} digitalio_digitalinout_obj_t;
