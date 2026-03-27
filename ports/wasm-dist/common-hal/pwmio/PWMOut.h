#pragma once
#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t duty_cycle;
    uint32_t frequency;
    bool variable_freq;
    bool enabled;
    bool never_reset;
} pwm_state_t;

extern pwm_state_t pwm_state[64];

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} pwmio_pwmout_obj_t;

void pwmio_reset_pwm_state(void);
