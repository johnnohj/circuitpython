/*
 * AnalogOut.c — Virtual DAC via /hal/analog fd endpoint.
 *
 * Same slot layout as AnalogIn (4 bytes per pin), with is_output=1.
 */

#include "common-hal/analogio/AnalogOut.h"
#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "supervisor/hal.h"
#include "py/runtime.h"

#include <string.h>
#include <unistd.h>

#define ANALOG_SLOT_SIZE 4

static void _write_pin(uint8_t pin, const uint8_t slot[ANALOG_SLOT_SIZE]) {
    int fd = hal_analog_fd();
    if (fd < 0) return;
    lseek(fd, pin * ANALOG_SLOT_SIZE, SEEK_SET);
    write(fd, slot, ANALOG_SLOT_SIZE);
}

static void _read_pin(uint8_t pin, uint8_t slot[ANALOG_SLOT_SIZE]) {
    int fd = hal_analog_fd();
    if (fd < 0) { memset(slot, 0, ANALOG_SLOT_SIZE); return; }
    lseek(fd, pin * ANALOG_SLOT_SIZE, SEEK_SET);
    ssize_t n = read(fd, slot, ANALOG_SLOT_SIZE);
    if (n < ANALOG_SLOT_SIZE) {
        memset(slot + (n > 0 ? n : 0), 0, ANALOG_SLOT_SIZE - (n > 0 ? n : 0));
    }
}

void common_hal_analogio_analogout_construct(analogio_analogout_obj_t *self,
    const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);
    hal_set_role(pin->number, HAL_ROLE_DAC);
    uint8_t slot[ANALOG_SLOT_SIZE] = {1, 1, 0, 0}; /* enabled, is_output, value=0 */
    _write_pin(pin->number, slot);
}

void common_hal_analogio_analogout_deinit(analogio_analogout_obj_t *self) {
    if (common_hal_analogio_analogout_deinited(self)) return;
    uint8_t slot[ANALOG_SLOT_SIZE] = {0};
    _write_pin(self->pin->number, slot);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogout_deinited(analogio_analogout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_analogio_analogout_set_value(analogio_analogout_obj_t *self,
    uint16_t value) {
    uint8_t slot[ANALOG_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[2] = value & 0xFF;
    slot[3] = (value >> 8) & 0xFF;
    _write_pin(self->pin->number, slot);
    hal_set_flag(self->pin->number, HAL_FLAG_C_WROTE);
}

void common_hal_analogio_analogout_never_reset(analogio_analogout_obj_t *self) {
    /* no-op */
}
