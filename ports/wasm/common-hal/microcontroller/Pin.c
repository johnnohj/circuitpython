/*
 * Pin.c — 64 virtual GPIO pins for WASI port.
 *
 * Adapted from ports/wasm/common-hal/microcontroller/Pin.c
 * Stripped: EMSCRIPTEN_KEEPALIVE, bank enable/disable UI.
 * Added: MEMFS state flush hook (called by worker poll loop).
 */

#include "common-hal/microcontroller/Pin.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"

#include <string.h>

/* ---- Pin definitions ---- */
/* All 64 pins start with full capabilities. The board layout in
 * peripherals/pins.c assigns names (D0, A0, SDA, LED, etc.). */

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
PIN_DEF(32); PIN_DEF(33); PIN_DEF(34); PIN_DEF(35);
PIN_DEF(36); PIN_DEF(37); PIN_DEF(38); PIN_DEF(39);
PIN_DEF(40); PIN_DEF(41); PIN_DEF(42); PIN_DEF(43);
PIN_DEF(44); PIN_DEF(45); PIN_DEF(46); PIN_DEF(47);
PIN_DEF(48); PIN_DEF(49); PIN_DEF(50); PIN_DEF(51);
PIN_DEF(52); PIN_DEF(53); PIN_DEF(54); PIN_DEF(55);
PIN_DEF(56); PIN_DEF(57); PIN_DEF(58); PIN_DEF(59);
PIN_DEF(60); PIN_DEF(61); PIN_DEF(62); PIN_DEF(63);

/* Lookup table for pin_number -> pin object */
static const mcu_pin_obj_t *const pin_table[64] = {
    &pin_GPIO0,  &pin_GPIO1,  &pin_GPIO2,  &pin_GPIO3,
    &pin_GPIO4,  &pin_GPIO5,  &pin_GPIO6,  &pin_GPIO7,
    &pin_GPIO8,  &pin_GPIO9,  &pin_GPIO10, &pin_GPIO11,
    &pin_GPIO12, &pin_GPIO13, &pin_GPIO14, &pin_GPIO15,
    &pin_GPIO16, &pin_GPIO17, &pin_GPIO18, &pin_GPIO19,
    &pin_GPIO20, &pin_GPIO21, &pin_GPIO22, &pin_GPIO23,
    &pin_GPIO24, &pin_GPIO25, &pin_GPIO26, &pin_GPIO27,
    &pin_GPIO28, &pin_GPIO29, &pin_GPIO30, &pin_GPIO31,
    &pin_GPIO32, &pin_GPIO33, &pin_GPIO34, &pin_GPIO35,
    &pin_GPIO36, &pin_GPIO37, &pin_GPIO38, &pin_GPIO39,
    &pin_GPIO40, &pin_GPIO41, &pin_GPIO42, &pin_GPIO43,
    &pin_GPIO44, &pin_GPIO45, &pin_GPIO46, &pin_GPIO47,
    &pin_GPIO48, &pin_GPIO49, &pin_GPIO50, &pin_GPIO51,
    &pin_GPIO52, &pin_GPIO53, &pin_GPIO54, &pin_GPIO55,
    &pin_GPIO56, &pin_GPIO57, &pin_GPIO58, &pin_GPIO59,
    &pin_GPIO60, &pin_GPIO61, &pin_GPIO62, &pin_GPIO63,
};

/* ---- Pin management ---- */

void reset_all_pins(void) {
    for (int i = 0; i < 64; i++) {
        mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin_table[i];
        if (!p->never_reset) {
            p->claimed = false;
        }
    }
}

void reset_pin_number(uint8_t pin_number) {
    if (pin_number < 64) {
        mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin_table[pin_number];
        p->claimed = false;
    }
}

void never_reset_pin_number(uint8_t pin_number) {
    if (pin_number < 64) {
        mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin_table[pin_number];
        p->never_reset = true;
    }
}

void claim_pin(const mcu_pin_obj_t *pin) {
    mcu_pin_obj_t *p = (mcu_pin_obj_t *)pin;
    p->claimed = true;
}

bool pin_number_is_free(uint8_t pin_number) {
    if (pin_number >= 64) {
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

/* validate_obj_is_free_pin is provided by shared-bindings/microcontroller/Pin.c */
