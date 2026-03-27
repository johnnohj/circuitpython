/*
 * DigitalInOut.c — Virtual GPIO for WASI port.
 *
 * Adapted from ports/wasm/common-hal/digitalio/DigitalInOut.c
 * Stripped: EMSCRIPTEN_KEEPALIVE, EM_JS, JsProxy.
 * State array is the source of truth; OPFS flush happens externally.
 */

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "hw_state.h"
#include "py/runtime.h"

#include <string.h>

gpio_pin_state_t gpio_state[64];

void digitalio_reset_gpio_state(void) {
    for (int i = 0; i < 64; i++) {
        if (gpio_state[i].never_reset) {
            continue;
        }
        gpio_state[i].value = false;
        gpio_state[i].direction = 0;
        gpio_state[i].pull = 0;
        gpio_state[i].open_drain = false;
        gpio_state[i].enabled = false;
    }
}

digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    uint8_t n = pin->number;
    gpio_state[n].enabled = true;
    gpio_state[n].direction = 0;
    gpio_state[n].pull = 0;
    gpio_state[n].value = false;
    gpio_state[n].open_drain = false;
    hw_gpio_dirty = true;
    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }
    gpio_state[self->pin->number].enabled = false;
    hw_gpio_dirty = true;
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    return gpio_state[self->pin->number].direction == 1
           ? DIRECTION_OUTPUT : DIRECTION_INPUT;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    uint8_t n = self->pin->number;
    gpio_state[n].direction = 0;
    gpio_state[n].pull = (uint8_t)pull;
    hw_gpio_dirty = true;
    return DIGITALINOUT_OK;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self, bool value,
    digitalio_drive_mode_t drive_mode) {
    uint8_t n = self->pin->number;
    gpio_state[n].direction = 1;
    gpio_state[n].value = value;
    gpio_state[n].open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);
    hw_gpio_dirty = true;
    return DIGITALINOUT_OK;
}

bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    return gpio_state[self->pin->number].value;
}

void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self,
    bool value) {
    gpio_state[self->pin->number].value = value;
    hw_gpio_dirty = true;
}

digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {
    return (digitalio_pull_t)gpio_state[self->pin->number].pull;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    gpio_state[self->pin->number].pull = (uint8_t)pull;
    hw_gpio_dirty = true;
    return DIGITALINOUT_OK;
}

digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {
    return gpio_state[self->pin->number].open_drain
           ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self, digitalio_drive_mode_t drive_mode) {
    gpio_state[self->pin->number].open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);
    hw_gpio_dirty = true;
    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    uint8_t n = self->pin->number;
    gpio_state[n].never_reset = true;
    never_reset_pin_number(n);
}
