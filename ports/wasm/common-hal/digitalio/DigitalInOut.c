/*
 * DigitalInOut.c — Virtual GPIO via /hal/gpio fd endpoint.
 *
 * Pin state lives at the /hal/gpio WASI fd, not in a C array.
 * Each pin occupies a fixed-size slot at offset pin_number * SLOT_SIZE.
 * wasi-memfs.js manages the state on the JS side and routes changes
 * to hardware simulators via onHardwareWrite.
 *
 * Slot layout (8 bytes per pin):
 *   [0] enabled    (uint8)
 *   [1] direction  (uint8: 0=input, 1=output)
 *   [2] value      (uint8: 0/1)
 *   [3] pull       (uint8: 0=none, 1=up, 2=down)
 *   [4] open_drain (uint8: 0/1)
 *   [5] never_reset(uint8: 0/1)
 *   [6-7] reserved
 */

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "supervisor/hal.h"
#include "py/runtime.h"

#include <string.h>
#include <unistd.h>

#define GPIO_SLOT_SIZE 8

/* Read a pin's slot from the /hal/gpio fd */
static void _read_pin(uint8_t pin, uint8_t slot[GPIO_SLOT_SIZE]) {
    int fd = hal_gpio_fd();
    if (fd < 0) {
        memset(slot, 0, GPIO_SLOT_SIZE);
        return;
    }
    lseek(fd, pin * GPIO_SLOT_SIZE, SEEK_SET);
    ssize_t n = read(fd, slot, GPIO_SLOT_SIZE);
    if (n < GPIO_SLOT_SIZE) {
        memset(slot + (n > 0 ? n : 0), 0, GPIO_SLOT_SIZE - (n > 0 ? n : 0));
    }
}

/* Write a pin's slot to the /hal/gpio fd */
static void _write_pin(uint8_t pin, const uint8_t slot[GPIO_SLOT_SIZE]) {
    int fd = hal_gpio_fd();
    if (fd < 0) {
        return;
    }
    lseek(fd, pin * GPIO_SLOT_SIZE, SEEK_SET);
    write(fd, slot, GPIO_SLOT_SIZE);
}

/* ------------------------------------------------------------------ */

digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    uint8_t slot[GPIO_SLOT_SIZE] = {0};
    slot[0] = 1;  /* enabled */
    _write_pin(pin->number, slot);
    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }
    uint8_t slot[GPIO_SLOT_SIZE] = {0}; /* enabled=0 */
    _write_pin(self->pin->number, slot);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return slot[1] == 1 ? DIRECTION_OUTPUT : DIRECTION_INPUT;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[1] = 0;  /* direction = input */
    slot[3] = (uint8_t)pull;
    /* Simulate pull resistor: pull-up reads HIGH, pull-down reads LOW
     * until an external source (JS setInputValue) drives it otherwise. */
    if (pull == PULL_UP) {
        slot[2] = 1;
    } else if (pull == PULL_DOWN) {
        slot[2] = 0;
    }
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self, bool value,
    digitalio_drive_mode_t drive_mode) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[1] = 1;  /* direction = output */
    slot[2] = value ? 1 : 0;
    slot[4] = (drive_mode == DRIVE_MODE_OPEN_DRAIN) ? 1 : 0;
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return slot[2] != 0;
}

void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self,
    bool value) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[2] = value ? 1 : 0;
    _write_pin(self->pin->number, slot);
}

digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return (digitalio_pull_t)slot[3];
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[3] = (uint8_t)pull;
    if (pull == PULL_UP) {
        slot[2] = 1;
    } else if (pull == PULL_DOWN) {
        slot[2] = 0;
    }
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return slot[4] ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self, digitalio_drive_mode_t drive_mode) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[4] = (drive_mode == DRIVE_MODE_OPEN_DRAIN) ? 1 : 0;
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    uint8_t slot[GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[5] = 1;
    _write_pin(self->pin->number, slot);
    never_reset_pin_number(self->pin->number);
}
