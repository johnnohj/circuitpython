// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/microcontroller/Pin.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// Pin.h — 32 virtual GPIO pins (GPIO_MAX_PINS) for WASM port.
//
// Pin state lives in port_mem.hal_gpio (MEMFS-in-linear-memory).
#pragma once

#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

// Capability flags
#define CAP_GPIO  (1 << 0)
#define CAP_ADC   (1 << 1)
#define CAP_DAC   (1 << 2)
#define CAP_PWM   (1 << 3)
#define CAP_I2C   (1 << 4)
#define CAP_SPI   (1 << 5)
#define CAP_UART  (1 << 6)
#define CAP_ALL   (CAP_GPIO | CAP_ADC | CAP_DAC | CAP_PWM | CAP_I2C | CAP_SPI | CAP_UART)

typedef struct {
    mp_obj_base_t base;
    uint8_t number;
    uint8_t capabilities;
    bool enabled;
    bool claimed;
    bool never_reset;
} mcu_pin_obj_t;

// 32 virtual pins (GPIO_MAX_PINS)
extern const mcu_pin_obj_t pin_GPIO0, pin_GPIO1, pin_GPIO2, pin_GPIO3;
extern const mcu_pin_obj_t pin_GPIO4, pin_GPIO5, pin_GPIO6, pin_GPIO7;
extern const mcu_pin_obj_t pin_GPIO8, pin_GPIO9, pin_GPIO10, pin_GPIO11;
extern const mcu_pin_obj_t pin_GPIO12, pin_GPIO13, pin_GPIO14, pin_GPIO15;
extern const mcu_pin_obj_t pin_GPIO16, pin_GPIO17, pin_GPIO18, pin_GPIO19;
extern const mcu_pin_obj_t pin_GPIO20, pin_GPIO21, pin_GPIO22, pin_GPIO23;
extern const mcu_pin_obj_t pin_GPIO24, pin_GPIO25, pin_GPIO26, pin_GPIO27;
extern const mcu_pin_obj_t pin_GPIO28, pin_GPIO29, pin_GPIO30, pin_GPIO31;

// Pin management
void reset_all_pins(void);
void reset_pin_number(uint8_t pin_number);
void never_reset_pin_number(uint8_t pin_number);
void claim_pin(const mcu_pin_obj_t *pin);
bool pin_number_is_free(uint8_t pin_number);
void common_hal_reset_pin(const mcu_pin_obj_t *pin);
void common_hal_never_reset_pin(const mcu_pin_obj_t *pin);
bool common_hal_mcu_pin_is_free(const mcu_pin_obj_t *pin);

// Validation
const mcu_pin_obj_t *validate_obj_is_free_pin(mp_obj_t obj, qstr arg_name);
