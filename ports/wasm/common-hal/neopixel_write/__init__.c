// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// NeoPixel implementation for WASM port

#include "shared-bindings/neopixel_write/__init__.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "py/runtime.h"
#include <emscripten.h>
#include <string.h>

// Maximum number of LEDs per pin (can be adjusted if needed)
#define MAX_LEDS_PER_PIN 256

// NeoPixel state per pin
typedef struct {
    uint8_t pixels[MAX_LEDS_PER_PIN * 3];  // RGB data (3 bytes per pixel)
    uint32_t num_bytes;                     // Number of bytes currently stored
    bool enabled;                           // Whether this pin has NeoPixels
} neopixel_pin_state_t;

// State array for all 64 GPIO pins
neopixel_pin_state_t neopixel_state[64];

EMSCRIPTEN_KEEPALIVE
neopixel_pin_state_t* get_neopixel_state_ptr(void) {
    return neopixel_state;
}

void neopixel_reset_state(void) {
    for (int i = 0; i < 64; i++) {
        memset(neopixel_state[i].pixels, 0, MAX_LEDS_PER_PIN * 3);
        neopixel_state[i].num_bytes = 0;
        neopixel_state[i].enabled = false;
    }
}

void common_hal_neopixel_write(const digitalio_digitalinout_obj_t *digitalinout,
                                uint8_t *pixels, uint32_t numBytes) {
    if (digitalinout->pin == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pin is deinit"));
    }

    uint8_t pin_num = digitalinout->pin->number;

    // Limit to maximum buffer size
    if (numBytes > MAX_LEDS_PER_PIN * 3) {
        numBytes = MAX_LEDS_PER_PIN * 3;
    }

    // Copy pixel data to our state array
    memcpy(neopixel_state[pin_num].pixels, pixels, numBytes);
    neopixel_state[pin_num].num_bytes = numBytes;
    neopixel_state[pin_num].enabled = true;
}
