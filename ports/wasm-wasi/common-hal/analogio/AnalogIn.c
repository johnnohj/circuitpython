#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogIn.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "hw_state.h"
#include "py/runtime.h"
#include <string.h>

analog_pin_state_t analog_state[64];

void analogio_reset_analog_state(void) {
    for (int i = 0; i < 64; i++) {
        analog_state[i].value = 32768;
        analog_state[i].is_output = false;
        analog_state[i].enabled = false;
    }
}

const mcu_pin_obj_t *common_hal_analogio_analogin_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);
}

void common_hal_analogio_analogin_construct(analogio_analogin_obj_t *self,
    const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);
    uint8_t n = pin->number;
    analog_state[n].is_output = false;
    analog_state[n].enabled = true;
    analog_state[n].value = 32768;
    hw_analog_dirty = true;
}

void common_hal_analogio_analogin_deinit(analogio_analogin_obj_t *self) {
    if (common_hal_analogio_analogin_deinited(self)) {
        return;
    }
    analog_state[self->pin->number].enabled = false;
    hw_analog_dirty = true;
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogin_deinited(analogio_analogin_obj_t *self) {
    return self->pin == NULL;
}

uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    return analog_state[self->pin->number].value;
}

float common_hal_analogio_analogin_get_reference_voltage(analogio_analogin_obj_t *self) {
    return 3.3f;
}
