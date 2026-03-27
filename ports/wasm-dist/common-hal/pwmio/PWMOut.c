/*
 * PWMOut.c — Virtual PWM via /hal/pwm fd endpoint.
 *
 * Slot layout (8 bytes per pin at offset pin * 8):
 *   [0]   enabled       (uint8)
 *   [1]   variable_freq (uint8)
 *   [2-3] duty_cycle    (uint16 LE)
 *   [4-7] frequency     (uint32 LE)
 */

#include "common-hal/pwmio/PWMOut.h"
#include "shared-bindings/pwmio/PWMOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "supervisor/hal.h"
#include "py/runtime.h"

#include <string.h>
#include <unistd.h>

#define PWM_SLOT_SIZE 8

static void _read_pin(uint8_t pin, uint8_t slot[PWM_SLOT_SIZE]) {
    int fd = hal_pwm_fd();
    if (fd < 0) { memset(slot, 0, PWM_SLOT_SIZE); return; }
    lseek(fd, pin * PWM_SLOT_SIZE, SEEK_SET);
    ssize_t n = read(fd, slot, PWM_SLOT_SIZE);
    if (n < PWM_SLOT_SIZE) {
        memset(slot + (n > 0 ? n : 0), 0, PWM_SLOT_SIZE - (n > 0 ? n : 0));
    }
}

static void _write_pin(uint8_t pin, const uint8_t slot[PWM_SLOT_SIZE]) {
    int fd = hal_pwm_fd();
    if (fd < 0) return;
    lseek(fd, pin * PWM_SLOT_SIZE, SEEK_SET);
    write(fd, slot, PWM_SLOT_SIZE);
}

static void _pack_slot(uint8_t slot[PWM_SLOT_SIZE], bool enabled, bool var_freq,
    uint16_t duty, uint32_t freq) {
    slot[0] = enabled ? 1 : 0;
    slot[1] = var_freq ? 1 : 0;
    slot[2] = duty & 0xFF;
    slot[3] = (duty >> 8) & 0xFF;
    slot[4] = freq & 0xFF;
    slot[5] = (freq >> 8) & 0xFF;
    slot[6] = (freq >> 16) & 0xFF;
    slot[7] = (freq >> 24) & 0xFF;
}

pwmout_result_t common_hal_pwmio_pwmout_construct(pwmio_pwmout_obj_t *self,
    const mcu_pin_obj_t *pin, uint16_t duty, uint32_t frequency,
    bool variable_frequency) {
    self->pin = pin;
    self->variable_freq = variable_frequency;
    claim_pin(pin);
    uint8_t slot[PWM_SLOT_SIZE];
    _pack_slot(slot, true, variable_frequency, duty, frequency);
    _write_pin(pin->number, slot);
    return PWMOUT_OK;
}

void common_hal_pwmio_pwmout_deinit(pwmio_pwmout_obj_t *self) {
    if (common_hal_pwmio_pwmout_deinited(self)) return;
    uint8_t slot[PWM_SLOT_SIZE] = {0};
    _write_pin(self->pin->number, slot);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_pwmio_pwmout_deinited(pwmio_pwmout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_pwmio_pwmout_set_duty_cycle(pwmio_pwmout_obj_t *self,
    uint16_t duty) {
    uint8_t slot[PWM_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[2] = duty & 0xFF;
    slot[3] = (duty >> 8) & 0xFF;
    _write_pin(self->pin->number, slot);
}

uint16_t common_hal_pwmio_pwmout_get_duty_cycle(pwmio_pwmout_obj_t *self) {
    uint8_t slot[PWM_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return (uint16_t)slot[2] | ((uint16_t)slot[3] << 8);
}

void common_hal_pwmio_pwmout_set_frequency(pwmio_pwmout_obj_t *self,
    uint32_t frequency) {
    if (!self->variable_freq) {
        mp_raise_ValueError(
            MP_ERROR_TEXT("PWM frequency not writable when variable_frequency is False"));
    }
    uint8_t slot[PWM_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[4] = frequency & 0xFF;
    slot[5] = (frequency >> 8) & 0xFF;
    slot[6] = (frequency >> 16) & 0xFF;
    slot[7] = (frequency >> 24) & 0xFF;
    _write_pin(self->pin->number, slot);
}

uint32_t common_hal_pwmio_pwmout_get_frequency(pwmio_pwmout_obj_t *self) {
    uint8_t slot[PWM_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return (uint32_t)slot[4] | ((uint32_t)slot[5] << 8) |
           ((uint32_t)slot[6] << 16) | ((uint32_t)slot[7] << 24);
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
