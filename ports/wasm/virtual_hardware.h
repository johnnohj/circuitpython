// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Initialize virtual hardware
void virtual_hardware_init(void);

// GPIO Operations (used by CircuitPython C code)
// These are the SINGLE SOURCE OF TRUTH - common-hal reads from here, doesn't duplicate
void virtual_gpio_set_direction(uint8_t pin, uint8_t direction);
void virtual_gpio_set_value(uint8_t pin, bool value);
bool virtual_gpio_get_value(uint8_t pin);
void virtual_gpio_set_pull(uint8_t pin, uint8_t pull);
void virtual_gpio_set_open_drain(uint8_t pin, bool open_drain);
bool virtual_gpio_get_open_drain(uint8_t pin);

// Analog Operations (used by CircuitPython C code)
void virtual_analog_init(uint8_t pin, bool is_output);
void virtual_analog_deinit(uint8_t pin);
uint16_t virtual_analog_read(uint8_t pin);
void virtual_analog_write(uint8_t pin, uint16_t value);

// JavaScript Interface - Input Simulation
// JavaScript can call these to simulate external inputs (buttons, sensors)
void virtual_gpio_set_input_value(uint8_t pin, bool value);
void virtual_analog_set_input_value(uint8_t pin, uint16_t value);

// JavaScript Interface - Output Observation
// JavaScript can call these to read outputs for visualization (LEDs, DAC)
bool virtual_gpio_get_output_value(uint8_t pin);
uint8_t virtual_gpio_get_direction(uint8_t pin);
uint8_t virtual_gpio_get_pull(uint8_t pin);
uint16_t virtual_analog_get_output_value(uint8_t pin);
bool virtual_analog_is_enabled(uint8_t pin);
bool virtual_analog_is_output(uint8_t pin);

// JavaScript Interface - Bulk Access
// For efficient visualization, JavaScript can access state arrays directly
typedef struct {
    bool value;
    uint8_t direction;
    uint8_t pull;
    bool open_drain;
    bool enabled;
} gpio_state_t;

typedef struct {
    uint16_t value;
    bool is_output;
    bool enabled;
} analog_state_t;

const gpio_state_t* virtual_gpio_get_state_array(void);
const analog_state_t* virtual_analog_get_state_array(void);
