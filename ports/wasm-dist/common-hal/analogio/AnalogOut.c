// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/analogio/AnalogOut.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// AnalogOut.c — Virtual DAC via direct memory access.
//
// Same analog slot layout as AnalogIn (4 bytes per pin), with is_output=1.

#include "common-hal/analogio/AnalogOut.h"
#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "port/hal.h"
#include "port/port_memory.h"
#include "port/constants.h"
#include "py/runtime.h"

#include <string.h>

void common_hal_analogio_analogout_construct(analogio_analogout_obj_t *self,
    const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);
    hal_set_role(pin->number, ROLE_DAC);

    uint8_t *slot = analog_slot(pin->number);
    slot[0] = 1;  // enabled
    slot[1] = 1;  // is_output
    slot[2] = 0;  // value low
    slot[3] = 0;  // value high
}

void common_hal_analogio_analogout_deinit(analogio_analogout_obj_t *self) {
    if (common_hal_analogio_analogout_deinited(self)) return;

    uint8_t *slot = analog_slot(self->pin->number);
    memset(slot, 0, PORT_ANALOG_SLOT_SIZE);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_analogio_analogout_deinited(analogio_analogout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_analogio_analogout_set_value(analogio_analogout_obj_t *self,
    uint16_t value) {
    uint8_t *slot = analog_slot(self->pin->number);
    slot[2] = value & 0xFF;
    slot[3] = (value >> 8) & 0xFF;
    hal_set_flag(self->pin->number, GF_C_WROTE);
}

void common_hal_analogio_analogout_never_reset(analogio_analogout_obj_t *self) {
    /* no-op */
}
