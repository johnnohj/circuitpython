// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "common-hal/analogio/AnalogIn.h"
#include "shared-bindings/analogio/AnalogIn.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "proxy_c.h"

// JavaScript semihosting functions for AnalogIn
bool js_analogin_create(const mcu_pin_obj_t *pin, int *js_ref_out) {
    if (pin->js_pin_proxy_ref < 0) {
        return false; // Pin not backed by JavaScript
    }
    
    // Call JavaScript to create an AnalogIn object for this pin
    uint32_t out[3];
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Call pin.createAnalogIn() method
        proxy_c_to_js_lookup_attr(pin->js_pin_proxy_ref, "createAnalogIn", out);
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

void js_analogin_deinit(int js_ref) {
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

uint16_t js_analogin_get_value(int js_ref) {
    if (js_ref < 0) return 0;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "value", out);
        mp_obj_t value_obj = proxy_convert_js_to_mp_obj_cside(out);
        
        uint16_t result = (uint16_t)mp_obj_get_int(value_obj);
        nlr_pop();
        return result;
    } else {
        nlr_pop();
        return 0;
    }
}

float js_analogin_get_reference_voltage(int js_ref) {
    if (js_ref < 0) return 3.3f; // Default reference voltage
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "reference_voltage", out);
        mp_obj_t voltage_obj = proxy_convert_js_to_mp_obj_cside(out);
        
        float result = mp_obj_get_float(voltage_obj);
        nlr_pop();
        return result;
    } else {
        nlr_pop();
        return 3.3f; // Default reference voltage
    }
}

// CircuitPython AnalogIn implementation
void common_hal_analogio_analogin_construct(analogio_analogin_obj_t *self,
                                           const mcu_pin_obj_t *pin) {
    claim_pin(pin);
    self->pin = pin;
    self->reference_voltage_mv = 3300; // Default 3.3V reference
    
    // Create JavaScript-backed AnalogIn
    if (!js_analogin_create(pin, &self->js_analogin_ref)) {
        self->js_analogin_ref = -1;
        mp_raise_RuntimeError(MP_ERROR_TEXT("Could not create JavaScript AnalogIn backend"));
    }
}

bool common_hal_analogio_analogin_deinited(analogio_analogin_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_analogio_analogin_deinit(analogio_analogin_obj_t *self) {
    if (common_hal_analogio_analogin_deinited(self)) {
        return;
    }
    
    js_analogin_deinit(self->js_analogin_ref);
    reset_pin_number(0, self->pin->number);
    self->pin = NULL;
    self->js_analogin_ref = -1;
}

uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    return js_analogin_get_value(self->js_analogin_ref);
}

float common_hal_analogio_analogin_get_reference_voltage(analogio_analogin_obj_t *self) {
    return js_analogin_get_reference_voltage(self->js_analogin_ref);
}