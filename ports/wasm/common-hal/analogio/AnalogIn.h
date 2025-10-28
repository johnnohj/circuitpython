// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// AnalogIn implementation for WASM port
// This is common-hal - the hardware abstraction layer
// For WASM, "hardware" = in-memory state arrays accessible to JavaScript

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

// Analog pin state for WASM virtual hardware
// This is the SINGLE SOURCE OF TRUTH for analog state
typedef struct {
    uint16_t value;      // 16-bit ADC/DAC value (0-65535)
    bool is_output;      // true=DAC, false=ADC
    bool enabled;        // Pin is in use
} analog_pin_state_t;

// 64 virtual analog pins - exposed to JavaScript via library.js
extern analog_pin_state_t analog_state[64];

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} analogio_analogin_obj_t;
