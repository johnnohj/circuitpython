#include "common-hal/analogio/AnalogOut.h"
#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "hw_opfs.h"
#include "py/runtime.h"

void common_hal_analogio_analogout_construct(analogio_analogout_obj_t *self,
    const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);
    uint8_t n = pin->number;
    analog_state[n].is_output = true;
    analog_state[n].enabled = true;
    analog_state[n].value = 0;
    hw_opfs_analog_dirty = true;
}

void common_hal_analogio_analogout_deinit(analogio_analogout_obj_t *self) {
    if (common_hal_analogio_analogout_deinited(self)) {
        return;
    }
    analog_state[self->pin->number].enabled = false;
    hw_opfs_analog_dirty = true;
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogout_deinited(analogio_analogout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_analogio_analogout_set_value(analogio_analogout_obj_t *self,
    uint16_t value) {
    analog_state[self->pin->number].value = value;
    hw_opfs_analog_dirty = true;
}

void common_hal_analogio_analogout_never_reset(analogio_analogout_obj_t *self) {
    /* no-op */
}
