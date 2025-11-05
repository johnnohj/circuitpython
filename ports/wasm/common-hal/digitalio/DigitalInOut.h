// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include "proxy_c.h"

// GPIO state for WASM virtual hardware
// Hybrid architecture: C structs for fast access + optional JsProxy for rich web features
typedef struct {
    // Fast path: Direct C state
    bool value;          // Current pin value
    uint8_t direction;   // 0=input, 1=output
    uint8_t pull;        // 0=none, 1=up, 2=down
    bool open_drain;     // Open-drain output mode
    bool enabled;        // Pin is enabled/in-use
    bool never_reset;    // If true, don't reset during soft reset

    // Rich path: Optional JsProxy for events (NULL if no web app listeners)
    // When this exists, JS Pin object is the source of truth
    // C code syncs to it via store_attr(), triggering automatic onChange events
    mp_obj_jsproxy_t *js_pin;
} gpio_pin_state_t;

// 64 virtual GPIO pins - exposed to JavaScript via library.js
extern gpio_pin_state_t gpio_state[64];

// DigitalInOut HAL object
// Only stores which pin this object controls
// Actual pin state is in gpio_state[] array
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;  // Which pin this object controls
} digitalio_digitalinout_obj_t;
