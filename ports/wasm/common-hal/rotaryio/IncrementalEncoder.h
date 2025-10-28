// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 Jeff Epler for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// WASM port uses software encoder (CIRCUITPY_ROTARYIO_SOFTENCODER)
// The shared-module provides quadrature decoding logic

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin_a;
    const mcu_pin_obj_t *pin_b;
    uint8_t encoder_index;  // Index into encoder_states array
    uint8_t state;          // Current AB state (used by shared-module)
    int8_t sub_count;       // Sub-count for divisor (used by shared-module)
    int8_t divisor;         // Number of quadrature edges per count
    mp_int_t position;      // Current encoder position
    bool never_reset;       // Preserve across soft resets
} rotaryio_incrementalencoder_obj_t;
