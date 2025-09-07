// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "proxy_c.h"

// JavaScript semihosting functions for DigitalInOut
bool js_digitalio_create(const mcu_pin_obj_t *pin, int *js_ref_out) {
    if (pin->js_pin_proxy_ref < 0) {
        return false; // Pin not backed by JavaScript
    }
    
    // Call JavaScript to create a DigitalInOut object for this pin
    uint32_t out[3];
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Call pin.createDigitalInOut() method
        proxy_c_to_js_lookup_attr(pin->js_pin_proxy_ref, "createDigitalInOut", out);
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

void js_digitalio_deinit(int js_ref) {
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

void js_digitalio_switch_to_input(int js_ref, digitalio_pull_t pull) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "switchToInput", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t pull_obj = mp_obj_new_int((int)pull);
            mp_call_function_1(method, pull_obj);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

void js_digitalio_switch_to_output(int js_ref, bool value, digitalio_drive_mode_t drive_mode) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "switchToOutput", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t args[2] = {
                mp_obj_new_bool(value),
                mp_obj_new_int((int)drive_mode)
            };
            mp_call_function_n_kw(method, 2, 0, args);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

bool js_digitalio_get_value(int js_ref) {
    if (js_ref < 0) return false;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "value", out);
        mp_obj_t value_obj = proxy_convert_js_to_mp_obj_cside(out);
        
        bool result = mp_obj_is_true(value_obj);
        nlr_pop();
        return result;
    } else {
        nlr_pop();
        return false;
    }
}

void js_digitalio_set_value(int js_ref, bool value) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "setValue", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t value_obj = mp_obj_new_bool(value);
            mp_call_function_1(method, value_obj);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

digitalio_direction_t js_digitalio_get_direction(int js_ref) {
    if (js_ref < 0) return DIRECTION_INPUT;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "direction", out);
        mp_obj_t direction_obj = proxy_convert_js_to_mp_obj_cside(out);
        
        int direction = mp_obj_get_int(direction_obj);
        nlr_pop();
        return (digitalio_direction_t)direction;
    } else {
        nlr_pop();
        return DIRECTION_INPUT;
    }
}

digitalio_pull_t js_digitalio_get_pull(int js_ref) {
    if (js_ref < 0) return PULL_NONE;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "pull", out);
        mp_obj_t pull_obj = proxy_convert_js_to_mp_obj_cside(out);
        
        int pull = mp_obj_get_int(pull_obj);
        nlr_pop();
        return (digitalio_pull_t)pull;
    } else {
        nlr_pop();
        return PULL_NONE;
    }
}

void js_digitalio_set_pull(int js_ref, digitalio_pull_t pull) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "setPull", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t pull_obj = mp_obj_new_int((int)pull);
            mp_call_function_1(method, pull_obj);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

digitalio_drive_mode_t js_digitalio_get_drive_mode(int js_ref) {
    if (js_ref < 0) return DRIVE_MODE_PUSH_PULL;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "driveMode", out);
        mp_obj_t drive_obj = proxy_convert_js_to_mp_obj_cside(out);
        
        int drive = mp_obj_get_int(drive_obj);
        nlr_pop();
        return (digitalio_drive_mode_t)drive;
    } else {
        nlr_pop();
        return DRIVE_MODE_PUSH_PULL;
    }
}

void js_digitalio_set_drive_mode(int js_ref, digitalio_drive_mode_t drive_mode) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "setDriveMode", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t drive_obj = mp_obj_new_int((int)drive_mode);
            mp_call_function_1(method, drive_obj);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

// CircuitPython DigitalInOut implementation
digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {
    
    claim_pin(pin);
    self->pin = pin;
    self->output = false;
    self->open_drain = false;
    self->pull = PULL_NONE;
    self->value = false;
    
    // Create JavaScript-backed DigitalInOut
    if (!js_digitalio_create(pin, &self->js_digitalinout_ref)) {
        self->js_digitalinout_ref = -1;
        return DIGITALINOUT_PIN_BUSY; // Could not create JS backend
    }
    
    return DIGITALINOUT_OK;
}

bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }
    
    js_digitalio_deinit(self->js_digitalinout_ref);
    reset_pin_number(0, self->pin->number);
    self->pin = NULL;
    self->js_digitalinout_ref = -1;
}

digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    return js_digitalio_get_direction(self->js_digitalinout_ref);
}

void common_hal_digitalio_digitalinout_set_direction(
    digitalio_digitalinout_obj_t *self, digitalio_direction_t direction) {
    
    self->output = (direction == DIRECTION_OUTPUT);
    
    if (direction == DIRECTION_OUTPUT) {
        digitalio_drive_mode_t drive = self->open_drain ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
        js_digitalio_switch_to_output(self->js_digitalinout_ref, self->value, drive);
    } else {
        js_digitalio_switch_to_input(self->js_digitalinout_ref, self->pull);
    }
}

bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    return js_digitalio_get_value(self->js_digitalinout_ref);
}

void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self, bool value) {
    self->value = value;
    js_digitalio_set_value(self->js_digitalinout_ref, value);
}

digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {
    return js_digitalio_get_drive_mode(self->js_digitalinout_ref);
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self, digitalio_drive_mode_t drive_mode) {
    
    self->open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);
    js_digitalio_set_drive_mode(self->js_digitalinout_ref, drive_mode);
    return DIGITALINOUT_OK;
}

digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {
    return js_digitalio_get_pull(self->js_digitalinout_ref);
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(digitalio_digitalinout_obj_t *self,
                                                digitalio_pull_t pull) {
    self->pull = pull;
    js_digitalio_set_pull(self->js_digitalinout_ref, pull);
    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    // WebAssembly pins can always reset - nothing to do
    (void)self;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    
    self->output = false;
    self->pull = pull;
    js_digitalio_switch_to_input(self->js_digitalinout_ref, pull);
    return DIGITALINOUT_OK;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self, bool value, digitalio_drive_mode_t drive_mode) {
    
    self->output = true;
    self->value = value;
    self->open_drain = (drive_mode == DRIVE_MODE_OPEN_DRAIN);
    js_digitalio_switch_to_output(self->js_digitalinout_ref, value, drive_mode);
    return DIGITALINOUT_OK;
}