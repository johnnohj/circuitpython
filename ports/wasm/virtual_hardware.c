// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Virtual hardware state for WASM port
// Simulates GPIO, analog, and bus peripherals entirely within WASM
// Provides JavaScript interface for input simulation and output observation

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <emscripten.h>

// GPIO state (64 pins)
// This is the SINGLE SOURCE OF TRUTH for GPIO state
// common-hal objects reference this, they don't duplicate it
typedef struct {
    bool value;          // Current pin value
    uint8_t direction;   // 0=input, 1=output
    uint8_t pull;        // 0=none, 1=up, 2=down
    bool open_drain;     // Open-drain output mode
    bool enabled;        // Pin is enabled
} gpio_state_t;

static gpio_state_t gpio_pins[64];

// Analog state
typedef struct {
    uint16_t value;      // 16-bit ADC/DAC value
    bool is_output;      // true=DAC, false=ADC
    bool enabled;
} analog_state_t;

static analog_state_t analog_pins[64];

// Initialize virtual hardware
void virtual_hardware_init(void) {
    // Initialize all GPIO pins
    for (int i = 0; i < 64; i++) {
        gpio_pins[i].value = false;
        gpio_pins[i].direction = 0;  // Input
        gpio_pins[i].pull = 0;       // No pull
        gpio_pins[i].open_drain = false;  // Push-pull by default
        gpio_pins[i].enabled = true;

        analog_pins[i].value = 32768;  // Mid-range
        analog_pins[i].is_output = false;
        analog_pins[i].enabled = false;
    }
}

// ============================================================================
// GPIO Operations
// ============================================================================

void virtual_gpio_set_direction(uint8_t pin, uint8_t direction) {
    if (pin < 64) {
        gpio_pins[pin].direction = direction;
    }
}

void virtual_gpio_set_value(uint8_t pin, bool value) {
    if (pin < 64 && gpio_pins[pin].direction == 1) { // Output only
        gpio_pins[pin].value = value;
    }
}

bool virtual_gpio_get_value(uint8_t pin) {
    if (pin < 64) {
        if (gpio_pins[pin].direction == 0) { // Input
            // For input pins, return simulated value based on pull
            if (gpio_pins[pin].pull == 1) return true;   // Pull-up
            if (gpio_pins[pin].pull == 2) return false;  // Pull-down
            return false; // Floating = low
        } else { // Output
            return gpio_pins[pin].value;
        }
    }
    return false;
}

void virtual_gpio_set_pull(uint8_t pin, uint8_t pull) {
    if (pin < 64) {
        gpio_pins[pin].pull = pull;
    }
}

void virtual_gpio_set_open_drain(uint8_t pin, bool open_drain) {
    if (pin < 64) {
        gpio_pins[pin].open_drain = open_drain;
    }
}

bool virtual_gpio_get_open_drain(uint8_t pin) {
    if (pin < 64) {
        return gpio_pins[pin].open_drain;
    }
    return false;
}

// ============================================================================
// Analog Operations
// ============================================================================

void virtual_analog_init(uint8_t pin, bool is_output) {
    if (pin < 64) {
        analog_pins[pin].is_output = is_output;
        analog_pins[pin].enabled = true;
        if (!is_output) {
            // ADC: Start with mid-range value
            analog_pins[pin].value = 32768;
        }
    }
}

void virtual_analog_deinit(uint8_t pin) {
    if (pin < 64) {
        analog_pins[pin].enabled = false;
    }
}

uint16_t virtual_analog_read(uint8_t pin) {
    if (pin < 64 && analog_pins[pin].enabled) {
        return analog_pins[pin].value;
    }
    return 0;
}

void virtual_analog_write(uint8_t pin, uint16_t value) {
    if (pin < 64 && analog_pins[pin].enabled && analog_pins[pin].is_output) {
        analog_pins[pin].value = value;
    }
}

// ============================================================================
// JavaScript Interface - Allow JS to interact with virtual hardware
// ============================================================================
//
// These functions allow JavaScript to:
// 1. Read output states (to visualize LEDs, PWM, etc.)
// 2. Write input states (to simulate buttons, sensors, etc.)
// 3. Query pin configuration
//
// This enables JavaScript to interact with WASM the same way the physical
// world interacts with a real board.

// GPIO JavaScript Interface

EMSCRIPTEN_KEEPALIVE
void virtual_gpio_set_input_value(uint8_t pin, bool value) {
    // JavaScript can simulate external inputs (buttons, sensors)
    if (pin < 64 && gpio_pins[pin].direction == 0) { // Only for inputs
        gpio_pins[pin].value = value;
    }
}

EMSCRIPTEN_KEEPALIVE
bool virtual_gpio_get_output_value(uint8_t pin) {
    // JavaScript can read output states for visualization
    if (pin < 64 && gpio_pins[pin].direction == 1) { // Only for outputs
        return gpio_pins[pin].value;
    }
    return false;
}

EMSCRIPTEN_KEEPALIVE
uint8_t virtual_gpio_get_direction(uint8_t pin) {
    // 0 = input, 1 = output
    if (pin < 64) {
        return gpio_pins[pin].direction;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t virtual_gpio_get_pull(uint8_t pin) {
    // 0 = none, 1 = up, 2 = down
    if (pin < 64) {
        return gpio_pins[pin].pull;
    }
    return 0;
}

// Analog JavaScript Interface

EMSCRIPTEN_KEEPALIVE
void virtual_analog_set_input_value(uint8_t pin, uint16_t value) {
    // JavaScript can simulate analog sensor readings
    if (pin < 64 && analog_pins[pin].enabled && !analog_pins[pin].is_output) {
        analog_pins[pin].value = value;
    }
}

EMSCRIPTEN_KEEPALIVE
uint16_t virtual_analog_get_output_value(uint8_t pin) {
    // JavaScript can read DAC output values
    if (pin < 64 && analog_pins[pin].enabled && analog_pins[pin].is_output) {
        return analog_pins[pin].value;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
bool virtual_analog_is_enabled(uint8_t pin) {
    if (pin < 64) {
        return analog_pins[pin].enabled;
    }
    return false;
}

EMSCRIPTEN_KEEPALIVE
bool virtual_analog_is_output(uint8_t pin) {
    if (pin < 64 && analog_pins[pin].enabled) {
        return analog_pins[pin].is_output;
    }
    return false;
}

// Bulk state access for efficient visualization

EMSCRIPTEN_KEEPALIVE
const gpio_state_t* virtual_gpio_get_state_array(void) {
    // Returns pointer to GPIO state array for direct memory access
    return gpio_pins;
}

EMSCRIPTEN_KEEPALIVE
const analog_state_t* virtual_analog_get_state_array(void) {
    // Returns pointer to analog state array for direct memory access
    return analog_pins;
}
