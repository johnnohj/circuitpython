// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// PWMOut implementation for WASM port

#include "common-hal/pwmio/PWMOut.h"
#include "shared-bindings/pwmio/PWMOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include <emscripten.h>
#include <string.h>

// 64 virtual PWM channels
pwm_state_t pwm_state[64];

EMSCRIPTEN_KEEPALIVE
pwm_state_t* get_pwm_state_ptr(void) {
    return pwm_state;
}

void pwmio_reset_pwm_state(void) {
    for (int i = 0; i < 64; i++) {
        // Skip channels marked as never_reset (e.g., display backlight, system fans)
        if (pwm_state[i].never_reset) {
            continue;
        }

        pwm_state[i].duty_cycle = 0;
        pwm_state[i].frequency = 500;
        pwm_state[i].variable_freq = true;
        pwm_state[i].enabled = false;
    }
}

pwmout_result_t common_hal_pwmio_pwmout_construct(pwmio_pwmout_obj_t *self,
    const mcu_pin_obj_t *pin, uint16_t duty, uint32_t frequency, bool variable_frequency) {
    
    self->pin = pin;
    claim_pin(pin);
    
    uint8_t pin_num = pin->number;
    pwm_state[pin_num].enabled = true;
    pwm_state[pin_num].duty_cycle = duty;
    pwm_state[pin_num].frequency = frequency;
    pwm_state[pin_num].variable_freq = variable_frequency;
    pwm_state[pin_num].never_reset = false;

    return PWMOUT_OK;
}

void common_hal_pwmio_pwmout_deinit(pwmio_pwmout_obj_t *self) {
    if (common_hal_pwmio_pwmout_deinited(self)) {
        return;
    }
    
    pwm_state[self->pin->number].enabled = false;
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_pwmio_pwmout_deinited(pwmio_pwmout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_pwmio_pwmout_set_duty_cycle(pwmio_pwmout_obj_t *self, uint16_t duty) {
    pwm_state[self->pin->number].duty_cycle = duty;
}

uint16_t common_hal_pwmio_pwmout_get_duty_cycle(pwmio_pwmout_obj_t *self) {
    return pwm_state[self->pin->number].duty_cycle;
}

void common_hal_pwmio_pwmout_set_frequency(pwmio_pwmout_obj_t *self, uint32_t frequency) {
    uint8_t pin_num = self->pin->number;
    
    if (!pwm_state[pin_num].variable_freq) {
        mp_raise_ValueError(MP_ERROR_TEXT("PWM frequency not writable when variable_frequency is False"));
    }
    
    pwm_state[pin_num].frequency = frequency;
}

uint32_t common_hal_pwmio_pwmout_get_frequency(pwmio_pwmout_obj_t *self) {
    return pwm_state[self->pin->number].frequency;
}

bool common_hal_pwmio_pwmout_get_variable_frequency(pwmio_pwmout_obj_t *self) {
    return pwm_state[self->pin->number].variable_freq;
}

const mcu_pin_obj_t *common_hal_pwmio_pwmout_get_pin(pwmio_pwmout_obj_t *self) {
    return self->pin;
}

void common_hal_pwmio_pwmout_never_reset(pwmio_pwmout_obj_t *self) {
    // Mark this PWM channel as never_reset so it persists across soft resets
    // This is important for display backlights and system-managed PWM
    uint8_t pin_num = self->pin->number;

    pwm_state[pin_num].never_reset = true;

    // Also mark the pin itself as never_reset at the microcontroller level
    never_reset_pin_number(pin_num);
}

void common_hal_pwmio_pwmout_reset_ok(pwmio_pwmout_obj_t *self) {
    // No-op for WASM
}
