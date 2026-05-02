// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-License-Identifier: MIT

// rotaryio IncrementalEncoder — reads quadrature from GPIO slots in port_mem.
//
// On each position read, samples pin_a and pin_b from their GPIO slots
// and applies the standard quadrature state machine.  No interrupts —
// the encoder is polled whenever Python reads .position.

#include "common-hal/rotaryio/IncrementalEncoder.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "port/constants.h"
#include "port/port_memory.h"

// Quadrature lookup: [old_state][new_state] → delta
// States: 0=00, 1=01, 2=10, 3=11
static const int8_t QUADRATURE_TABLE[4][4] = {
    //        00   01   10   11   ← new
    /* 00 */ { 0,  -1,  +1,   0},
    /* 01 */ {+1,   0,   0,  -1},
    /* 10 */ {-1,   0,   0,  +1},
    /* 11 */ { 0,  +1,  -1,   0},
};

static uint8_t read_pin(const mcu_pin_obj_t *pin) {
    uint8_t *slot = gpio_slot(pin->number);
    return slot[GPIO_OFF_VALUE] ? 1 : 0;
}

static void update_state(rotaryio_incrementalencoder_obj_t *self) {
    uint8_t new_state = (read_pin(self->pin_a) << 1) | read_pin(self->pin_b);
    int8_t delta = QUADRATURE_TABLE[self->state][new_state];
    self->state = new_state;
    self->sub_count += delta;
    if (self->sub_count >= self->divisor) {
        self->position++;
        self->sub_count = 0;
    } else if (self->sub_count <= -self->divisor) {
        self->position--;
        self->sub_count = 0;
    }
}

void common_hal_rotaryio_incrementalencoder_construct(
    rotaryio_incrementalencoder_obj_t *self,
    const mcu_pin_obj_t *pin_a, const mcu_pin_obj_t *pin_b) {

    claim_pin(pin_a);
    claim_pin(pin_b);

    self->pin_a = pin_a;
    self->pin_b = pin_b;
    self->position = 0;
    self->sub_count = 0;
    self->divisor = 4;  // default: 4 edges per detent

    // Configure pins as inputs with pull-up via GPIO slots
    uint8_t *slot_a = gpio_slot(pin_a->number);
    slot_a[GPIO_OFF_ENABLED] = 1;
    slot_a[GPIO_OFF_DIRECTION] = 0;  // input
    slot_a[GPIO_OFF_PULL] = 1;       // pull-up

    uint8_t *slot_b = gpio_slot(pin_b->number);
    slot_b[GPIO_OFF_ENABLED] = 1;
    slot_b[GPIO_OFF_DIRECTION] = 0;
    slot_b[GPIO_OFF_PULL] = 1;

    // Read initial state
    self->state = (read_pin(pin_a) << 1) | read_pin(pin_b);
}

void common_hal_rotaryio_incrementalencoder_deinit(rotaryio_incrementalencoder_obj_t *self) {
    if (common_hal_rotaryio_incrementalencoder_deinited(self)) {
        return;
    }
    reset_pin_number(self->pin_a->number);
    reset_pin_number(self->pin_b->number);
    self->pin_a = NULL;
    self->pin_b = NULL;
}

bool common_hal_rotaryio_incrementalencoder_deinited(rotaryio_incrementalencoder_obj_t *self) {
    return self->pin_a == NULL;
}

mp_int_t common_hal_rotaryio_incrementalencoder_get_position(rotaryio_incrementalencoder_obj_t *self) {
    update_state(self);
    return self->position;
}

void common_hal_rotaryio_incrementalencoder_set_position(rotaryio_incrementalencoder_obj_t *self,
    mp_int_t new_position) {
    self->position = new_position;
    self->sub_count = 0;
}

mp_int_t common_hal_rotaryio_incrementalencoder_get_divisor(rotaryio_incrementalencoder_obj_t *self) {
    return self->divisor;
}

void common_hal_rotaryio_incrementalencoder_set_divisor(rotaryio_incrementalencoder_obj_t *self,
    mp_int_t new_divisor) {
    self->divisor = new_divisor > 0 ? new_divisor : 1;
}
