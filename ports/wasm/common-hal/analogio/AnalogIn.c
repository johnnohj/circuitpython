/*
 * AnalogIn.c — Virtual ADC via /hal/analog fd endpoint.
 *
 * Slot layout (4 bytes per pin at offset pin * 4):
 *   [0] enabled    (uint8)
 *   [1] is_output  (uint8)
 *   [2-3] value    (uint16 LE)
 */

#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogIn.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "supervisor/hal.h"
#include "py/runtime.h"

#include <string.h>
#include <unistd.h>

#define ANALOG_SLOT_SIZE 4

static void _read_pin(uint8_t pin, uint8_t slot[ANALOG_SLOT_SIZE]) {
    int fd = hal_analog_fd();
    if (fd < 0) { memset(slot, 0, ANALOG_SLOT_SIZE); return; }
    lseek(fd, pin * ANALOG_SLOT_SIZE, SEEK_SET);
    ssize_t n = read(fd, slot, ANALOG_SLOT_SIZE);
    if (n < ANALOG_SLOT_SIZE) {
        memset(slot + (n > 0 ? n : 0), 0, ANALOG_SLOT_SIZE - (n > 0 ? n : 0));
    }
}

static void _write_pin(uint8_t pin, const uint8_t slot[ANALOG_SLOT_SIZE]) {
    int fd = hal_analog_fd();
    if (fd < 0) return;
    lseek(fd, pin * ANALOG_SLOT_SIZE, SEEK_SET);
    write(fd, slot, ANALOG_SLOT_SIZE);
}

const mcu_pin_obj_t *common_hal_analogio_analogin_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);
}

void common_hal_analogio_analogin_construct(analogio_analogin_obj_t *self,
    const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);
    hal_set_role(pin->number, HAL_ROLE_ADC);
    uint8_t slot[ANALOG_SLOT_SIZE] = {1, 0, 0x00, 0x80}; /* enabled, input, value=32768 */
    _write_pin(pin->number, slot);
}

void common_hal_analogio_analogin_deinit(analogio_analogin_obj_t *self) {
    if (common_hal_analogio_analogin_deinited(self)) return;
    uint8_t slot[ANALOG_SLOT_SIZE] = {0};
    _write_pin(self->pin->number, slot);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogin_deinited(analogio_analogin_obj_t *self) {
    return self->pin == NULL;
}

uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    uint8_t slot[ANALOG_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    uint8_t flags = hal_get_flags(self->pin->number);
    if (flags & HAL_FLAG_JS_WROTE) {
        hal_clear_flag(self->pin->number, HAL_FLAG_JS_WROTE);
        hal_set_flag(self->pin->number, HAL_FLAG_C_READ);
    }
    return (uint16_t)slot[2] | ((uint16_t)slot[3] << 8);
}

float common_hal_analogio_analogin_get_reference_voltage(analogio_analogin_obj_t *self) {
    return 3.3f;
}
