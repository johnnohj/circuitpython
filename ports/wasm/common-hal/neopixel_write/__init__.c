// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/neopixel_write/__init__.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// neopixel_write — Virtual NeoPixel via port_mem.
//
// Writes pixel data into the /hal/neopixel region of port_mem.
// JS reads this via live view to render NeoPixel LEDs on the board SVG.
//
// Format (matches ports/wasm convention):
//   [0]   pin number  (uint8)
//   [1]   enabled     (uint8, 1 = active)
//   [2-3] num_bytes   (uint16 LE)
//   [4+]  pixel data  (GRB or GRBW bytes)

#include "shared-bindings/neopixel_write/__init__.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "port/hal.h"
#include "port/port_memory.h"
#include "port/constants.h"
#include "py/runtime.h"

#include <string.h>

void common_hal_neopixel_write(const digitalio_digitalinout_obj_t *digitalinout,
    uint8_t *pixels, uint32_t numBytes) {
    if (digitalinout->pin == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pin is deinit"));
        return;
    }

    uint8_t pin = digitalinout->pin->number;

    // Clamp to region capacity
    if (numBytes > PORT_NEOPIXEL_MAX_BYTES) {
        numBytes = PORT_NEOPIXEL_MAX_BYTES;
    }

    // Write header
    uint8_t *np = port_mem.hal_neopixel;
    np[0] = pin;
    np[1] = 1;  // enabled
    np[2] = numBytes & 0xFF;
    np[3] = (numBytes >> 8) & 0xFF;

    // Copy pixel data
    memcpy(np + PORT_NEOPIXEL_HEADER, pixels, numBytes);

    // Mark GPIO slot dirty so JS knows to re-read
    uint8_t *slot = gpio_slot(pin);
    slot[GPIO_FLAGS] |= GF_C_WROTE;
    hal_mark_gpio_dirty(pin);
}
