// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"
#include "proxy_c.h"

// WebAssembly JavaScript-backed pin implementation

// Pin type definition
const mp_obj_type_t mcu_pin_type = {
    { &mp_type_type },
    .name = MP_QSTR_Pin,
};

// Optional virtual pins for testing
#ifdef CIRCUITPY_INCLUDE_VIRTUAL_PINS
const mcu_pin_obj_t pin_VIRTUAL_LED = PIN(255);
const mcu_pin_obj_t pin_VIRTUAL_BUTTON = PIN(254);
#endif

// Global pin state tracking
static bool pins_reset[256] = {false}; // Track which pins need reset
static bool pins_claimed[256] = {false}; // Track claimed pins
static bool pins_never_reset[256] = {false}; // Track pins that should never reset

void reset_all_pins(void) {
    // Reset all JavaScript-backed pins
    for (int i = 0; i < 256; i++) {
        if (pins_reset[i] && !pins_never_reset[i]) {
            reset_pin_number(0, i); // WebAssembly uses single port
        }
        pins_claimed[i] = false;
    }
}

void reset_pin_number(uint8_t pin_port, uint8_t pin_number) {
    // WebAssembly ignores pin_port (always 0)
    (void)pin_port;
    
    pins_claimed[pin_number] = false;
    pins_reset[pin_number] = false;
    
    // TODO: Could call JavaScript reset function for this pin
}

void claim_pin(const mcu_pin_obj_t *pin) {
    pins_claimed[pin->number] = true;
}

bool pin_number_is_free(uint8_t pin_port, uint8_t pin_number) {
    (void)pin_port; // WebAssembly ignores pin_port
    return !pins_claimed[pin_number];
}

void never_reset_pin_number(uint8_t pin_port, uint8_t pin_number) {
    (void)pin_port; // WebAssembly ignores pin_port
    pins_never_reset[pin_number] = true;
}

uint16_t pin_mask(uint8_t pin_number) {
    return 1 << (pin_number & 0x0F); // Simple bitmask for WebAssembly
}

// JavaScript semihosting function to create dynamic pins
mp_obj_t mp_js_create_pin(uint32_t *js_pin_ref, uint8_t pin_number, uint32_t capabilities) {
    js_pin_obj_t *pin = m_new_obj(js_pin_obj_t);
    pin->base.type = &mcu_pin_type;
    pin->number = pin_number;
    pin->js_pin_proxy_ref = ((uint32_t*)js_pin_ref)[1]; // Extract JS reference
    pin->capabilities = capabilities;
    
    // Mark pin as available for reset
    pins_reset[pin_number] = true;
    
    return MP_OBJ_FROM_PTR(pin);
}

// Get pin capabilities for JavaScript semihosting
uint32_t mp_js_pin_get_capabilities(mp_obj_t pin_obj) {
    if (!mp_obj_is_type(pin_obj, &mcu_pin_type)) {
        return 0;
    }
    js_pin_obj_t *pin = MP_OBJ_TO_PTR(pin_obj);
    return pin->capabilities;
}

// Call JavaScript pin method
mp_obj_t mp_js_pin_call_method(mp_obj_t pin_obj, const char *method_name, size_t n_args, const mp_obj_t *args) {
    if (!mp_obj_is_type(pin_obj, &mcu_pin_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("expected pin"));
    }
    
    js_pin_obj_t *pin = MP_OBJ_TO_PTR(pin_obj);
    if (pin->js_pin_proxy_ref < 0) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("pin not backed by JavaScript"));
    }
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Look up the method on the JavaScript pin object
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(pin->js_pin_proxy_ref, method_name, out);
        mp_obj_t js_method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (!mp_obj_is_callable(js_method)) {
            mp_raise_AttributeError(MP_ERROR_TEXT("pin method not found"));
        }
        
        // Call the JavaScript method
        mp_obj_t result = mp_call_function_n_kw(js_method, n_args, 0, args);
        nlr_pop();
        return result;
    } else {
        // Exception occurred
        nlr_pop();
        mp_raise_RuntimeError(MP_ERROR_TEXT("JavaScript pin method call failed"));
    }
}