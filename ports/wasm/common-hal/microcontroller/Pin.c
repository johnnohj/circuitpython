// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - virtual pins for JavaScript simulation

#include "py/runtime.h"
#include "shared-bindings/microcontroller/Pin.h"

// ============================================================================
// Pin Definition Macro
// ============================================================================

// WASM port: All pins are fully capable (no hardware limitations)
#define ALL_CAPS (CAP_GPIO | CAP_ADC | CAP_DAC | CAP_PWM | CAP_I2C | CAP_SPI | CAP_UART)

#define PIN_GPIO(p_number) \
    mcu_pin_obj_t pin_GPIO##p_number = { \
        { &mcu_pin_type }, \
        .number = p_number, \
        .enabled = true, \
        .capabilities = ALL_CAPS, \
        .claimed = false, \
        .never_reset = false \
    }

// ============================================================================
// GPIO BANK 0 (GPIO0-15)
// ============================================================================

PIN_GPIO(0);
PIN_GPIO(1);
PIN_GPIO(2);
PIN_GPIO(3);
PIN_GPIO(4);
PIN_GPIO(5);
PIN_GPIO(6);
PIN_GPIO(7);
PIN_GPIO(8);
PIN_GPIO(9);
PIN_GPIO(10);
PIN_GPIO(11);
PIN_GPIO(12);
PIN_GPIO(13);
PIN_GPIO(14);
PIN_GPIO(15);

// ============================================================================
// GPIO BANK 1 (GPIO16-31)
// ============================================================================

PIN_GPIO(16);
PIN_GPIO(17);
PIN_GPIO(18);
PIN_GPIO(19);
PIN_GPIO(20);
PIN_GPIO(21);
PIN_GPIO(22);
PIN_GPIO(23);
PIN_GPIO(24);
PIN_GPIO(25);
PIN_GPIO(26);
PIN_GPIO(27);
PIN_GPIO(28);
PIN_GPIO(29);
PIN_GPIO(30);
PIN_GPIO(31);

// ============================================================================
// GPIO BANK 2 (GPIO32-47)
// ============================================================================

PIN_GPIO(32);
PIN_GPIO(33);
PIN_GPIO(34);
PIN_GPIO(35);
PIN_GPIO(36);
PIN_GPIO(37);
PIN_GPIO(38);
PIN_GPIO(39);
PIN_GPIO(40);
PIN_GPIO(41);
PIN_GPIO(42);
PIN_GPIO(43);
PIN_GPIO(44);
PIN_GPIO(45);
PIN_GPIO(46);
PIN_GPIO(47);

// ============================================================================
// GPIO BANK 3 (GPIO48-63)
// ============================================================================

PIN_GPIO(48);
PIN_GPIO(49);
PIN_GPIO(50);
PIN_GPIO(51);
PIN_GPIO(52);
PIN_GPIO(53);
PIN_GPIO(54);
PIN_GPIO(55);
PIN_GPIO(56);
PIN_GPIO(57);
PIN_GPIO(58);
PIN_GPIO(59);
PIN_GPIO(60);
PIN_GPIO(61);
PIN_GPIO(62);
PIN_GPIO(63);

// ============================================================================
// Pin Lookup Table
// ============================================================================

static mcu_pin_obj_t *all_pins[] = {
    // Bank 0 (GPIO0-15)
    &pin_GPIO0, &pin_GPIO1, &pin_GPIO2, &pin_GPIO3,
    &pin_GPIO4, &pin_GPIO5, &pin_GPIO6, &pin_GPIO7,
    &pin_GPIO8, &pin_GPIO9, &pin_GPIO10, &pin_GPIO11,
    &pin_GPIO12, &pin_GPIO13, &pin_GPIO14, &pin_GPIO15,

    // Bank 1 (GPIO16-31)
    &pin_GPIO16, &pin_GPIO17, &pin_GPIO18, &pin_GPIO19,
    &pin_GPIO20, &pin_GPIO21, &pin_GPIO22, &pin_GPIO23,
    &pin_GPIO24, &pin_GPIO25, &pin_GPIO26, &pin_GPIO27,
    &pin_GPIO28, &pin_GPIO29, &pin_GPIO30, &pin_GPIO31,

    // Bank 2 (GPIO32-47)
    &pin_GPIO32, &pin_GPIO33, &pin_GPIO34, &pin_GPIO35,
    &pin_GPIO36, &pin_GPIO37, &pin_GPIO38, &pin_GPIO39,
    &pin_GPIO40, &pin_GPIO41, &pin_GPIO42, &pin_GPIO43,
    &pin_GPIO44, &pin_GPIO45, &pin_GPIO46, &pin_GPIO47,

    // Bank 3 (GPIO48-63)
    &pin_GPIO48, &pin_GPIO49, &pin_GPIO50, &pin_GPIO51,
    &pin_GPIO52, &pin_GPIO53, &pin_GPIO54, &pin_GPIO55,
    &pin_GPIO56, &pin_GPIO57, &pin_GPIO58, &pin_GPIO59,
    &pin_GPIO60, &pin_GPIO61, &pin_GPIO62, &pin_GPIO63,
};

// ============================================================================
// Helper Functions
// ============================================================================

mcu_pin_obj_t *get_pin_by_number(uint8_t pin_number) {
    if (pin_number >= 64) {
        return NULL;
    }
    return all_pins[pin_number];
}

void enable_gpio_bank_0(bool enable) {
    for (int i = 0; i < 16; i++) {
        all_pins[i]->enabled = enable;
    }
}

void enable_gpio_bank_1(bool enable) {
    for (int i = 16; i < 32; i++) {
        all_pins[i]->enabled = enable;
    }
}

void enable_gpio_bank_2(bool enable) {
    for (int i = 32; i < 48; i++) {
        all_pins[i]->enabled = enable;
    }
}

void enable_gpio_bank_3(bool enable) {
    for (int i = 48; i < 64; i++) {
        all_pins[i]->enabled = enable;
    }
}

// ============================================================================
// Pin Management Functions
// ============================================================================

void reset_all_pins(void) {
    for (int i = 0; i < 64; i++) {
        if (!all_pins[i]->never_reset && all_pins[i]->enabled) {
            reset_pin_number(i);
        }
    }
}

void reset_pin_number(uint8_t pin_number) {
    if (pin_number >= 64) {
        return;
    }

    mcu_pin_obj_t *pin = all_pins[pin_number];
    if (!pin->never_reset) {
        pin->claimed = false;
        // Reset hardware state via message queue would go here
    }
}

void never_reset_pin_number(uint8_t pin_number) {
    if (pin_number >= 64) {
        return;
    }
    all_pins[pin_number]->never_reset = true;
}

void claim_pin(const mcu_pin_obj_t *pin) {
    if (pin == NULL || pin->number >= 64) {
        return;
    }

    // Mark pin as claimed
    mcu_pin_obj_t *mutable_pin = all_pins[pin->number];
    mutable_pin->claimed = true;
}

bool pin_number_is_free(uint8_t pin_number) {
    if (pin_number >= 64) {
        return false;
    }

    mcu_pin_obj_t *pin = all_pins[pin_number];
    return pin->enabled && !pin->claimed;
}

// ============================================================================
// Common HAL Functions
// ============================================================================

void common_hal_never_reset_pin(const mcu_pin_obj_t *pin) {
    never_reset_pin_number(pin->number);
}

void common_hal_reset_pin(const mcu_pin_obj_t *pin) {
    reset_pin_number(pin->number);
}

bool common_hal_mcu_pin_is_free(const mcu_pin_obj_t *pin) {
    return pin_number_is_free(pin->number);
}

uint8_t common_hal_mcu_pin_number(const mcu_pin_obj_t *pin) {
    return pin->number;
}

void common_hal_mcu_pin_claim(const mcu_pin_obj_t *pin) {
    claim_pin(pin);
}

void common_hal_mcu_pin_claim_number(uint8_t pin_no) {
    if (pin_no >= 64) {
        return;
    }
    all_pins[pin_no]->claimed = true;
}

void common_hal_mcu_pin_reset_number(uint8_t pin_no) {
    reset_pin_number(pin_no);
}
