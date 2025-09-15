// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "common-hal/analogio/AnalogOut.h"
#include "shared-bindings/analogio/AnalogOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "proxy_c.h"

// JavaScript semihosting functions for AnalogOut
bool js_analogout_create(const mcu_pin_obj_t *pin, int *js_ref_out) {
    if (pin->js_pin_proxy_ref < 0) {
        return false; // Pin not backed by JavaScript
    }
    
    // Call JavaScript to create an AnalogOut object for this pin
    uint32_t out[3];
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Call pin.createAnalogOut() method
        proxy_c_to_js_lookup_attr(pin->js_pin_proxy_ref, "createAnalogOut", out);
        mp_obj_t create_method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(create_method)) {
            mp_obj_t result = mp_call_function_0(create_method);
            
            // Extract JavaScript reference from result
            if (mp_obj_is_type(result, &mp_type_int)) {
                *js_ref_out = mp_obj_get_int(result);
                nlr_pop();
                return true;
            }
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return false;
}

void js_analogout_deinit(int js_ref) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "deinit", out);
        mp_obj_t deinit_method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(deinit_method)) {
            mp_call_function_0(deinit_method);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

void js_analogout_set_value(int js_ref, uint16_t value) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "setValue", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t value_obj = mp_obj_new_int(value);
            mp_call_function_1(method, value_obj);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

// CircuitPython AnalogOut implementation
void common_hal_analogio_analogout_construct(analogio_analogout_obj_t *self,
                                            const mcu_pin_obj_t *pin) {
    claim_pin(pin);
    self->pin = pin;
    self->value = 0;
    
    // Create JavaScript-backed AnalogOut
    if (!js_analogout_create(pin, &self->js_analogout_ref)) {
        self->js_analogout_ref = -1;
        mp_raise_RuntimeError(MP_ERROR_TEXT("Could not create JavaScript AnalogOut backend"));
    }
}

bool common_hal_analogio_analogout_deinited(analogio_analogout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_analogio_analogout_deinit(analogio_analogout_obj_t *self) {
    if (common_hal_analogio_analogout_deinited(self)) {
        return;
    }
    
    js_analogout_deinit(self->js_analogout_ref);
    reset_pin_number(0, self->pin->number);
    self->pin = NULL;
    self->js_analogout_ref = -1;
}

void common_hal_analogio_analogout_set_value(analogio_analogout_obj_t *self, uint16_t val) {
    self->value = val;
    js_analogout_set_value(self->js_analogout_ref, val);
}