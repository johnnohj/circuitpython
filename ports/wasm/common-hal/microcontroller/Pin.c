// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/microcontroller/Pin.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// Pin.c — 32 virtual GPIO pins (GPIO_MAX_PINS) for WASM port.
//
// Pin state lives in port_mem.hal_gpio (MEMFS-in-linear-memory).

#include "common-hal/microcontroller/Pin.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "port/hal.h"
#include "port/constants.h"
#include "py/runtime.h"

#include <string.h>

// ---- Pin definitions ----
// All 32 pins start with full capabilities. The board layout in
// board/board_pins.c assigns names (D0, A0, SDA, LED, etc.).

#define PIN_DEF(n) \
    const mcu_pin_obj_t pin_GPIO##n = { \
        .base = { .type = &mcu_pin_type }, \
        .number = (n), \
        .capabilities = CAP_ALL, \
        .enabled = true, \
        .claimed = false, \
        .never_reset = false, \
    }

PIN_DEF(0);  PIN_DEF(1);  PIN_DEF(2);  PIN_DEF(3);
PIN_DEF(4);  PIN_DEF(5);  PIN_DEF(6);  PIN_DEF(7);
PIN_DEF(8);  PIN_DEF(9);  PIN_DEF(10); PIN_DEF(11);
PIN_DEF(12); PIN_DEF(13); PIN_DEF(14); PIN_DEF(15);
PIN_DEF(16); PIN_DEF(17); PIN_DEF(18); PIN_DEF(19);
PIN_DEF(20); PIN_DEF(21); PIN_DEF(22); PIN_DEF(23);
PIN_DEF(24); PIN_DEF(25); PIN_DEF(26); PIN_DEF(27);
PIN_DEF(28); PIN_DEF(29); PIN_DEF(30); PIN_DEF(31);

// Lookup table for pin_number -> pin object
static const mcu_pin_obj_t *const pin_table[GPIO_MAX_PINS] = {
    &pin_GPIO0,  &pin_GPIO1,  &pin_GPIO2,  &pin_GPIO3,
    &pin_GPIO4,  &pin_GPIO5,  &pin_GPIO6,  &pin_GPIO7,
    &pin_GPIO8,  &pin_GPIO9,  &pin_GPIO10, &pin_GPIO11,
    &pin_GPIO12, &pin_GPIO13, &pin_GPIO14, &pin_GPIO15,
    &pin_GPIO16, &pin_GPIO17, &pin_GPIO18, &pin_GPIO19,
    &pin_GPIO20, &pin_GPIO21, &pin_GPIO22, &pin_GPIO23,
    &pin_GPIO24, &pin_GPIO25, &pin_GPIO26, &pin_GPIO27,
    &pin_GPIO28, &pin_GPIO29, &pin_GPIO30, &pin_GPIO31,
};

// ---- Pin management ----

void reset_all_pins(void) {
    for (int i = 0; i < GPIO_MAX_PINS; i++) {
        mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin_table[i];
        if (!p->never_reset) {
            p->claimed = false;
        }
    }
}

void reset_pin_number(uint8_t pin_number) {
    if (pin_number < GPIO_MAX_PINS) {
        mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin_table[pin_number];
        p->claimed = false;
        hal_clear_role(pin_number);  // clears role + flags, preserves category
    }
}

void never_reset_pin_number(uint8_t pin_number) {
    if (pin_number < GPIO_MAX_PINS) {
        mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin_table[pin_number];
        p->never_reset = true;
    }
}

void claim_pin(const mcu_pin_obj_t *pin) {
    mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin;
    p->claimed = true;
}

void common_hal_mcu_pin_claim(const mcu_pin_obj_t *pin) {
    claim_pin(pin);
}

bool pin_number_is_free(uint8_t pin_number) {
    if (pin_number >= GPIO_MAX_PINS) {
        return false;
    }
    return !pin_table[pin_number]->claimed;
}

void common_hal_reset_pin(const mcu_pin_obj_t *pin) {
    if (pin == NULL) {
        return;
    }
    reset_pin_number(pin->number);
}

void common_hal_never_reset_pin(const mcu_pin_obj_t *pin) {
    if (pin == NULL) {
        return;
    }
    never_reset_pin_number(pin->number);
}

bool common_hal_mcu_pin_is_free(const mcu_pin_obj_t *pin) {
    return pin_number_is_free(pin->number);
}

// validate_obj_is_free_pin is provided by shared-bindings/microcontroller/Pin.c
