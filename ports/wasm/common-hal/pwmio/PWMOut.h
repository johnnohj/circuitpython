// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/pwmio/PWMOut.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// PWMOut.h — Virtual PWM via direct memory access.
#pragma once
#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    bool variable_freq;
    uint16_t duty_cycle;
    uint32_t frequency;
} pwmio_pwmout_obj_t;
