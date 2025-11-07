// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// DigitalInOut implementation for WASM port
// This is common-hal - the hardware abstraction layer
// For WASM, "hardware" = in-memory state arrays accessible to JavaScript

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/Pull.h"
#include "shared-bindings/digitalio/DriveMode.h"
#include "shared-bindings/digitalio/Direction.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "proxy_c.h"
#include <emscripten.h>
#include <string.h>

// GPIO state array - this is the "virtual hardware" for the WASM port
// JavaScript accesses this via library.js to read outputs and write inputs
gpio_pin_state_t gpio_state[64];

// Expose pointer to JavaScript via library.js
EMSCRIPTEN_KEEPALIVE
gpio_pin_state_t* get_gpio_state_ptr(void) {
    return gpio_state;
}

// ============================================================================
// JsProxy Integration for Rich Web Features
// ============================================================================

// Create a JS Pin object and add it to the proxy system
// Returns the js_ref index for use with mp_obj_new_jsproxy()
EM_JS(int, gpio_create_js_pin_proxy, (int pin_number), {
    // Detect execution context
    const isWorker = typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope;
    const context = isWorker ? 'WORKER' : 'MAIN';
    console.log(`[${context}] gpio_create_js_pin_proxy called for pin ${pin_number}`);

    // Get the GPIO controller from the peripheral system
    const gpio = Module.getPeripheral('gpio');
    if (!gpio) {
        console.warn(`[${context}] GPIO peripheral not initialized`);
        return -1;  // Indicate failure
    }

    // Get the Pin object (controller creates it if it doesn't exist)
    const pin = gpio.getPin(pin_number);

    // Add to proxy system and return the reference
    // This makes the JS Pin object accessible from C code
    return proxy_js_add_obj(pin);
});

// Post GPIO state update to main thread (if running in worker)
EM_JS(void, gpio_post_state_update, (int pin_number, int direction, int value), {
    if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope) {
        // We're in a worker - post message to main thread immediately
        // Direct postMessage (not setTimeout) ensures updates stream in real-time
        self.postMessage({
            type: 'gpio-update',
            pin: pin_number,
            direction: direction === 0 ? 'input' : 'output',
            value: value !== 0
        });
    }
});

// Helper function to sync a boolean value to JS Pin proxy
// This triggers the JS setter which automatically fires onChange callbacks
static inline void gpio_sync_bool_to_js_pin(mp_obj_jsproxy_t *js_pin, const char *attr_name, bool value) {
    if (js_pin == NULL) {
        return;  // No JS proxy, skip sync
    }

    uint32_t value_out[PVN];
    proxy_convert_mp_to_js_obj_cside(mp_obj_new_bool(value), value_out);
    store_attr(js_pin->ref, attr_name, value_out);
}

// Helper function to sync an integer value to JS Pin proxy
__attribute__((unused))
static inline void gpio_sync_int_to_js_pin(mp_obj_jsproxy_t *js_pin, const char *attr_name, int value) {
    if (js_pin == NULL) {
        return;  // No JS proxy, skip sync
    }

    uint32_t value_out[PVN];
    proxy_convert_mp_to_js_obj_cside(mp_obj_new_int(value), value_out);
    store_attr(js_pin->ref, attr_name, value_out);
}

// Helper function to sync a string value to JS Pin proxy
__attribute__((unused))
static inline void gpio_sync_str_to_js_pin(mp_obj_jsproxy_t *js_pin, const char *attr_name, const char *value) {
    if (js_pin == NULL) {
        return;  // No JS proxy, skip sync
    }

    uint32_t value_out[PVN];
    proxy_convert_mp_to_js_obj_cside(mp_obj_new_str(value, strlen(value)), value_out);
    store_attr(js_pin->ref, attr_name, value_out);
}

// Initialize GPIO state (called during port initialization)
void digitalio_reset_gpio_state(void) {
    for (int i = 0; i < 64; i++) {
        // Skip pins marked as never_reset (e.g., used by displayio or supervisor)
        if (gpio_state[i].never_reset) {
            continue;
        }

        gpio_state[i].value = false;
        gpio_state[i].direction = 0;  // Input
        gpio_state[i].pull = 0;       // No pull
        gpio_state[i].open_drain = false;
        gpio_state[i].enabled = false;
        gpio_state[i].js_pin = NULL;  // No JsProxy by default
    }
}

// Validate that an object is a valid pin
const mcu_pin_obj_t *common_hal_digitalio_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);
}

// Construct a DigitalInOut object
digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self,
    const mcu_pin_obj_t *pin) {

    // Claim the pin and store it
    claim_pin(pin);
    self->pin = pin;

    // Initialize GPIO state to input mode (default)
    uint8_t pin_num = pin->number;
    gpio_state[pin_num].direction = 0;     // Input
    gpio_state[pin_num].pull = 0;          // No pull
    gpio_state[pin_num].open_drain = false;
    gpio_state[pin_num].enabled = true;
    gpio_state[pin_num].never_reset = false;

    // Create JavaScript Pin proxy for rich web features
    int js_ref = gpio_create_js_pin_proxy(pin_num);
    if (js_ref >= 0) {
        gpio_state[pin_num].js_pin = mp_obj_new_jsproxy(js_ref);
    } else {
        gpio_state[pin_num].js_pin = NULL;
    }

    return DIGITALINOUT_OK;
}

// Deinitialize the pin
void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }
    // Mark GPIO as disabled
    gpio_state[self->pin->number].enabled = false;
    // Mark object as deinited
    self->pin = NULL;
}

// Check if deinited
bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

// Switch to input mode
digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self,
    digitalio_pull_t pull) {

    uint8_t pin_num = self->pin->number;

    // Fast path: Update C state
    gpio_state[pin_num].direction = 0;  // Input
    gpio_state[pin_num].pull = (pull == PULL_UP) ? 1 :
                                (pull == PULL_DOWN) ? 2 : 0;

    // Rich path: Sync to JsProxy if it exists
    gpio_sync_str_to_js_pin(gpio_state[pin_num].js_pin, "direction", "input");

    const char *pull_str = (pull == PULL_UP) ? "up" :
                           (pull == PULL_DOWN) ? "down" : "none";
    gpio_sync_str_to_js_pin(gpio_state[pin_num].js_pin, "pull", pull_str);

    // Post update to main thread
    gpio_post_state_update(pin_num, 0, gpio_state[pin_num].value);

    return DIGITALINOUT_OK;
}

// Switch to output mode
digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self,
    bool value,
    digitalio_drive_mode_t drive_mode) {

    uint8_t pin_num = self->pin->number;

    // Fast path: Update C state
    gpio_state[pin_num].direction = 1;  // Output
    gpio_state[pin_num].value = value;
    gpio_state[pin_num].open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);

    // Rich path: Sync to JsProxy if it exists
    gpio_sync_str_to_js_pin(gpio_state[pin_num].js_pin, "direction", "output");
    gpio_sync_bool_to_js_pin(gpio_state[pin_num].js_pin, "value", value);
    gpio_sync_str_to_js_pin(gpio_state[pin_num].js_pin, "driveMode",
        (drive_mode == DRIVE_MODE_OPEN_DRAIN) ? "open-drain" : "push-pull");

    // Post update to main thread
    gpio_post_state_update(pin_num, 1, value);

    return DIGITALINOUT_OK;
}

// Get direction
digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {

    return (gpio_state[self->pin->number].direction == 0) ?
           DIRECTION_INPUT : DIRECTION_OUTPUT;
}

// Set pin value (output mode)
void common_hal_digitalio_digitalinout_set_value(
    digitalio_digitalinout_obj_t *self,
    bool value) {

    uint8_t pin_num = self->pin->number;

    // Only allow setting value if pin is output
    if (gpio_state[pin_num].direction == 1) {
        // Fast path: Update C state
        gpio_state[pin_num].value = value;

        // Rich path: Sync to JsProxy if it exists (triggers automatic onChange events)
        gpio_sync_bool_to_js_pin(gpio_state[pin_num].js_pin, "value", value);

        // Post update to main thread
        gpio_post_state_update(pin_num, gpio_state[pin_num].direction, value);
    }
}

// Get pin value
bool common_hal_digitalio_digitalinout_get_value(
    digitalio_digitalinout_obj_t *self) {

    uint8_t pin_num = self->pin->number;

    if (gpio_state[pin_num].direction == 0) {
        // Input: return simulated value based on pull resistor
        // (JavaScript can override this by writing to gpio_state)
        if (gpio_state[pin_num].pull == 1) return true;   // Pull-up
        if (gpio_state[pin_num].pull == 2) return false;  // Pull-down
        return gpio_state[pin_num].value;  // Use stored value (JS-set)
    } else {
        // Output: return current output value
        return gpio_state[pin_num].value;
    }
}

// Set drive mode
digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self,
    digitalio_drive_mode_t drive_mode) {

    gpio_state[self->pin->number].open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);
    return DIGITALINOUT_OK;
}

// Get drive mode
digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {

    return gpio_state[self->pin->number].open_drain ?
           DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

// Set pull resistor
digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self,
    digitalio_pull_t pull) {

    gpio_state[self->pin->number].pull = (pull == PULL_UP) ? 1 :
                                          (pull == PULL_DOWN) ? 2 : 0;

    return DIGITALINOUT_OK;
}

// Get pull resistor
digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {

    uint8_t pull = gpio_state[self->pin->number].pull;
    if (pull == 1) return PULL_UP;
    if (pull == 2) return PULL_DOWN;
    return PULL_NONE;
}

// Never reset
void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    // Mark this GPIO pin as never_reset so it persists across soft resets
    // This is important for display control pins and system-managed GPIOs
    uint8_t pin_num = self->pin->number;

    gpio_state[pin_num].never_reset = true;

    // Also mark the pin itself as never_reset at the microcontroller level
    never_reset_pin_number(pin_num);
}

// Get register (not supported in WASM)
volatile uint32_t *common_hal_digitalio_digitalinout_get_reg(
    digitalio_digitalinout_obj_t *self,
    digitalinout_reg_op_t op,
    uint32_t *mask) {
    return NULL;
}

// Check if register operations are supported
bool common_hal_digitalio_has_reg_op(digitalinout_reg_op_t op) {
    return false;  // Not supported in WASM
}
