// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/pwmio/PWMOut.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// PWMOut.c — Virtual PWM via direct memory access.
//
// PWM state is stored in the GPIO slot for the pin.  The GPIO slot's
// role field is set to ROLE_PWM; the actual duty/frequency are stored
// in a port_mem-backed PWM region.  For now, we reuse the GPIO slot
// with duty/frequency packed into the reserved bytes and a separate
// per-pin struct in the PWMOut object.
//
// Slot layout concept (8 bytes per pin):
//   [0]   enabled       (uint8)
//   [1]   variable_freq (uint8)
//   [2-3] duty_cycle    (uint16 LE)
//   [4-7] frequency     (uint32 LE)
//
// Since PWM doesn't have its own MEMFS region yet, we store
// duty/frequency in the object and push to GPIO slot metadata.

#include "common-hal/pwmio/PWMOut.h"
#include "shared-bindings/pwmio/PWMOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "port/hal.h"
#include "port/port_memory.h"
#include "port/constants.h"
#include "py/runtime.h"

#include <string.h>

pwmout_result_t common_hal_pwmio_pwmout_construct(pwmio_pwmout_obj_t *self,
    const mcu_pin_obj_t *pin, uint16_t duty, uint32_t frequency,
    bool variable_frequency) {
    self->pin = pin;
    self->variable_freq = variable_frequency;
    self->duty_cycle = duty;
    self->frequency = frequency;
    claim_pin(pin);
    hal_set_role(pin->number, ROLE_PWM);

    uint8_t *slot = gpio_slot(pin->number);
    slot[GPIO_ENABLED] = HAL_ENABLED_YES;
    slot[GPIO_FLAGS] |= GF_C_WROTE;
    return PWMOUT_OK;
}

void common_hal_pwmio_pwmout_deinit(pwmio_pwmout_obj_t *self) {
    if (common_hal_pwmio_pwmout_deinited(self)) return;

    uint8_t *slot = gpio_slot(self->pin->number);
    memset(slot, 0, GPIO_SLOT_SIZE);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_pwmio_pwmout_deinited(pwmio_pwmout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_pwmio_pwmout_set_duty_cycle(pwmio_pwmout_obj_t *self,
    uint16_t duty) {
    self->duty_cycle = duty;
    hal_set_flag(self->pin->number, GF_C_WROTE);
}

uint16_t common_hal_pwmio_pwmout_get_duty_cycle(pwmio_pwmout_obj_t *self) {
    return self->duty_cycle;
}

void common_hal_pwmio_pwmout_set_frequency(pwmio_pwmout_obj_t *self,
    uint32_t frequency) {
    if (!self->variable_freq) {
        mp_raise_ValueError(
            MP_ERROR_TEXT("PWM frequency not writable when variable_frequency is False"));
    }
    self->frequency = frequency;
}

uint32_t common_hal_pwmio_pwmout_get_frequency(pwmio_pwmout_obj_t *self) {
    return self->frequency;
}

bool common_hal_pwmio_pwmout_get_variable_frequency(pwmio_pwmout_obj_t *self) {
    return self->variable_freq;
}

const mcu_pin_obj_t *common_hal_pwmio_pwmout_get_pin(pwmio_pwmout_obj_t *self) {
    return self->pin;
}

void common_hal_pwmio_pwmout_never_reset(pwmio_pwmout_obj_t *self) {
    never_reset_pin_number(self->pin->number);
}

void common_hal_pwmio_pwmout_reset_ok(pwmio_pwmout_obj_t *self) {
    /* no-op */
}
