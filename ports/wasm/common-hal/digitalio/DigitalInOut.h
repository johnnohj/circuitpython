// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// DigitalInOut HAL object
// Note: This does NOT store pin state (direction, pull, open_drain, value)
// Those are stored in virtual_hardware.c - the single source of truth
// This only stores the pin reference (which pin this object controls)
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;  // Which pin this object controls
} digitalio_digitalinout_obj_t;
