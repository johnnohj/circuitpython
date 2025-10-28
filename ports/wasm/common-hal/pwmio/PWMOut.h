// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// PWM state - exposed to JavaScript for visualization
typedef struct {
    uint16_t duty_cycle;
    uint32_t frequency;
    bool variable_freq;
    bool enabled;
    bool never_reset;    // If true, don't reset during soft reset
} pwm_state_t;

extern pwm_state_t pwm_state[64];

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} pwmio_pwmout_obj_t;
