// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"
#include "common-hal/microcontroller/Pin.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin_a;
    const mcu_pin_obj_t *pin_b;
    uint8_t state;       // <old_a><old_b>
    int8_t sub_count;    // intermediate transitions between detents
    int8_t divisor;      // quadrature edges per count
    mp_int_t position;
} rotaryio_incrementalencoder_obj_t;
