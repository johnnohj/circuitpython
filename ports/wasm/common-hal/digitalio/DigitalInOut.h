// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/digitalio/DigitalInOut.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// DigitalInOut.h — Virtual GPIO via direct memory access.
//
// Pin state lives in port_mem.hal_gpio, accessed via gpio_slot().
#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} digitalio_digitalinout_obj_t;
