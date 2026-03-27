/*
 * PWMOut.h — Virtual PWM via /hal/pwm fd endpoint.
 */
#pragma once
#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    bool variable_freq;
} pwmio_pwmout_obj_t;
