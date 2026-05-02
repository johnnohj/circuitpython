// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-License-Identifier: MIT

// countio Counter — edge counting from GPIO slots in port_mem.
//
// Polled on each .count read.  Detects rising/falling edges by
// comparing current pin value against last seen value.

#include "common-hal/countio/Counter.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "shared-bindings/countio/Edge.h"
#include "port/constants.h"
#include "port/port_memory.h"

static uint8_t read_pin(const mcu_pin_obj_t *pin) {
    uint8_t *slot = gpio_slot(pin->number);
    return slot[GPIO_VALUE] ? 1 : 0;
}

void common_hal_countio_counter_construct(countio_counter_obj_t *self,
    const mcu_pin_obj_t *pin, countio_edge_t edge, digitalio_pull_t pull) {

    claim_pin(pin);
    self->pin = pin;
    self->edge = edge;
    self->count = 0;

    // Configure pin as input via GPIO slot
    uint8_t *slot = gpio_slot(pin->number);
    slot[GPIO_ENABLED] = 1;
    slot[GPIO_DIRECTION] = 0;  // input
    if (pull == PULL_UP) {
        slot[GPIO_PULL] = 1;
    } else if (pull == PULL_DOWN) {
        slot[GPIO_PULL] = 2;
    }

    self->last_value = read_pin(pin);
}

bool common_hal_countio_counter_deinited(countio_counter_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_countio_counter_deinit(countio_counter_obj_t *self) {
    if (common_hal_countio_counter_deinited(self)) {
        return;
    }
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

mp_int_t common_hal_countio_counter_get_count(countio_counter_obj_t *self) {
    uint8_t current = read_pin(self->pin);
    if (current != self->last_value) {
        bool rising = current && !self->last_value;
        bool falling = !current && self->last_value;
        if ((rising && self->edge != EDGE_FALL) ||
            (falling && self->edge != EDGE_RISE)) {
            self->count++;
        }
        self->last_value = current;
    }
    return self->count;
}

void common_hal_countio_counter_set_count(countio_counter_obj_t *self,
    mp_int_t new_count) {
    self->count = new_count;
}
