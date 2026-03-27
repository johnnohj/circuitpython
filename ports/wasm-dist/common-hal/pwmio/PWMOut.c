#include "common-hal/pwmio/PWMOut.h"
#include "shared-bindings/pwmio/PWMOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "hw_state.h"
#include "py/runtime.h"

pwm_state_t pwm_state[64];

void pwmio_reset_pwm_state(void) {
    for (int i = 0; i < 64; i++) {
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
    const mcu_pin_obj_t *pin, uint16_t duty, uint32_t frequency,
    bool variable_frequency) {
    self->pin = pin;
    claim_pin(pin);
    uint8_t n = pin->number;
    pwm_state[n].enabled = true;
    pwm_state[n].duty_cycle = duty;
    pwm_state[n].frequency = frequency;
    pwm_state[n].variable_freq = variable_frequency;
    pwm_state[n].never_reset = false;
    hw_pwm_dirty = true;
    return PWMOUT_OK;
}

void common_hal_pwmio_pwmout_deinit(pwmio_pwmout_obj_t *self) {
    if (common_hal_pwmio_pwmout_deinited(self)) {
        return;
    }
    pwm_state[self->pin->number].enabled = false;
    hw_pwm_dirty = true;
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_pwmio_pwmout_deinited(pwmio_pwmout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_pwmio_pwmout_set_duty_cycle(pwmio_pwmout_obj_t *self,
    uint16_t duty) {
    pwm_state[self->pin->number].duty_cycle = duty;
    hw_pwm_dirty = true;
}

uint16_t common_hal_pwmio_pwmout_get_duty_cycle(pwmio_pwmout_obj_t *self) {
    return pwm_state[self->pin->number].duty_cycle;
}

void common_hal_pwmio_pwmout_set_frequency(pwmio_pwmout_obj_t *self,
    uint32_t frequency) {
    uint8_t n = self->pin->number;
    if (!pwm_state[n].variable_freq) {
        mp_raise_ValueError(
            MP_ERROR_TEXT("PWM frequency not writable when variable_frequency is False"));
    }
    pwm_state[n].frequency = frequency;
    hw_pwm_dirty = true;
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
    pwm_state[self->pin->number].never_reset = true;
    never_reset_pin_number(self->pin->number);
}

void common_hal_pwmio_pwmout_reset_ok(pwmio_pwmout_obj_t *self) {
    /* no-op */
}
