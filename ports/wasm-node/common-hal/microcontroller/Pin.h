// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/mphal.h"
#include "py/obj.h"

// WebAssembly JavaScript-backed pin implementation

typedef struct {
    mp_obj_base_t base;
    uint8_t number;
    int js_pin_proxy_ref;     // Reference to JavaScript pin implementation
    uint32_t capabilities;    // Bitmask of pin capabilities (GPIO, PWM, ADC, etc.)
} js_pin_obj_t;

// Pin capability flags
#define JS_PIN_CAP_DIGITAL_IO   (1 << 0)
#define JS_PIN_CAP_ANALOG_IN    (1 << 1)
#define JS_PIN_CAP_ANALOG_OUT   (1 << 2)
#define JS_PIN_CAP_PWM          (1 << 3)
#define JS_PIN_CAP_SPI          (1 << 4)
#define JS_PIN_CAP_I2C          (1 << 5)
#define JS_PIN_CAP_UART         (1 << 6)

// For compatibility with CircuitPython pin system
typedef js_pin_obj_t mcu_pin_obj_t;

#define PIN(p_number)       \
    { \
        { &mcu_pin_type }, \
        .number = p_number, \
        .js_pin_proxy_ref = -1, \
        .capabilities = JS_PIN_CAP_DIGITAL_IO \
    }

// Optional virtual pins for testing
#ifdef CIRCUITPY_INCLUDE_VIRTUAL_PINS
extern const mcu_pin_obj_t pin_VIRTUAL_LED;
extern const mcu_pin_obj_t pin_VIRTUAL_BUTTON;
#endif

// Pin management functions
void reset_all_pins(void);
void reset_pin_number(uint8_t pin_port, uint8_t pin_number);
void claim_pin(const mcu_pin_obj_t *pin);
bool pin_number_is_free(uint8_t pin_port, uint8_t pin_number);
void never_reset_pin_number(uint8_t pin_port, uint8_t pin_number);
uint16_t pin_mask(uint8_t pin_number);

// JavaScript semihosting functions
mp_obj_t mp_js_create_pin(uint32_t *js_pin_ref, uint8_t pin_number, uint32_t capabilities);
const mp_obj_dict_t *get_board_module_dict(void);
void mp_js_register_board_pins(uint32_t *pins_array, size_t num_pins);