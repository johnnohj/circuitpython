// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"
#include "common-hal/microcontroller/Pin.h"
#include "shared-bindings/countio/Edge.h"
#include "shared-bindings/digitalio/Pull.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    countio_edge_t edge;
    mp_int_t count;
    uint8_t last_value;
} countio_counter_obj_t;
