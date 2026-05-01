// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/analogio/AnalogIn.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// AnalogIn.c — Virtual ADC via direct memory access.
//
// Analog slot layout (4 bytes per pin via analog_slot()):
//   [0] enabled    (uint8)
//   [1] is_output  (uint8)
//   [2-3] value    (uint16 LE)

#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogIn.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "port/hal.h"
#include "port/port_memory.h"
#include "port/constants.h"
#include "py/runtime.h"

#include <string.h>

const mcu_pin_obj_t *common_hal_analogio_analogin_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);
}

void common_hal_analogio_analogin_construct(analogio_analogin_obj_t *self,
    const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);
    hal_set_role(pin->number, ROLE_ADC);

    uint8_t *slot = analog_slot(pin->number);
    slot[0] = 1;     // enabled
    slot[1] = 0;     // input
    slot[2] = 0x00;  // value low byte (32768 = 0x8000)
    slot[3] = 0x80;  // value high byte
}

void common_hal_analogio_analogin_deinit(analogio_analogin_obj_t *self) {
    if (common_hal_analogio_analogin_deinited(self)) return;

    uint8_t *slot = analog_slot(self->pin->number);
    memset(slot, 0, PORT_ANALOG_SLOT_SIZE);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogin_deinited(analogio_analogin_obj_t *self) {
    return self->pin == NULL;
}

uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    uint8_t *slot = analog_slot(self->pin->number);
    uint8_t flags = hal_get_flags(self->pin->number);
    if (flags & GF_JS_WROTE) {
        hal_clear_flag(self->pin->number, GF_JS_WROTE);
        hal_set_flag(self->pin->number, GF_C_READ);
    }
    return (uint16_t)slot[2] | ((uint16_t)slot[3] << 8);
}

float common_hal_analogio_analogin_get_reference_voltage(analogio_analogin_obj_t *self) {
    return 3.3f;
}
