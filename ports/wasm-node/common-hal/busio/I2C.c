// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "py/mperrno.h"

#include "common-hal/busio/I2C.h"
#include "shared-bindings/busio/I2C.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "proxy_c.h"

// JavaScript semihosting functions for I2C
bool js_i2c_create(const mcu_pin_obj_t *scl_pin, const mcu_pin_obj_t *sda_pin, 
                   uint32_t frequency, int *js_ref_out) {
    // Create JavaScript I2C object using board-level I2C creation
    uint32_t out[3];
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // Call global createI2C function with pins and frequency
        proxy_c_to_js_lookup_attr(0, "createI2C", out); // Global function
        mp_obj_t create_method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(create_method)) {
            mp_obj_t args[3] = {
                mp_obj_new_int(scl_pin->number),
                mp_obj_new_int(sda_pin->number),
                mp_obj_new_int(frequency)
            };
            mp_obj_t result = mp_call_function_n_kw(create_method, 3, 0, args);
            
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

void js_i2c_deinit(int js_ref) {
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

bool js_i2c_try_lock(int js_ref) {
    if (js_ref < 0) return false;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "tryLock", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t result = mp_call_function_0(method);
            bool success = mp_obj_is_true(result);
            nlr_pop();
            return success;
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return false;
}

bool js_i2c_has_lock(int js_ref) {
    if (js_ref < 0) return false;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "hasLock", out);
        mp_obj_t property = proxy_convert_js_to_mp_obj_cside(out);
        
        bool has_lock = mp_obj_is_true(property);
        nlr_pop();
        return has_lock;
    } else {
        nlr_pop();
        return false;
    }
}

void js_i2c_unlock(int js_ref) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "unlock", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_call_function_0(method);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

uint8_t js_i2c_probe_for_device(int js_ref, uint8_t address) {
    if (js_ref < 0) return 1; // Error
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "scan", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t addr_obj = mp_obj_new_int(address);
            mp_obj_t result = mp_call_function_1(method, addr_obj);
            uint8_t status = (uint8_t)mp_obj_get_int(result);
            nlr_pop();
            return status;
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return 1; // Error
}

uint8_t js_i2c_write(int js_ref, uint16_t address, const uint8_t *data, size_t len, bool stop) {
    if (js_ref < 0) return 1; // Error
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "writeto", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            // Create JavaScript Uint8Array from data
            mp_obj_t data_array = mp_obj_new_bytes(data, len);
            mp_obj_t args[3] = {
                mp_obj_new_int(address),
                data_array,
                mp_obj_new_bool(stop)
            };
            mp_obj_t result = mp_call_function_n_kw(method, 3, 0, args);
            uint8_t status = (uint8_t)mp_obj_get_int(result);
            nlr_pop();
            return status;
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return 1; // Error
}

uint8_t js_i2c_read(int js_ref, uint16_t address, uint8_t *data, size_t len) {
    if (js_ref < 0) return 1; // Error
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "readfrom", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t args[2] = {
                mp_obj_new_int(address),
                mp_obj_new_int(len)
            };
            mp_obj_t result = mp_call_function_n_kw(method, 2, 0, args);
            
            // Extract data from result (assumed to be bytes object)
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(result, &bufinfo, MP_BUFFER_READ);
            memcpy(data, bufinfo.buf, MIN(len, bufinfo.len));
            
            nlr_pop();
            return 0; // Success
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return 1; // Error
}

uint8_t js_i2c_write_read(int js_ref, uint16_t address, 
                          const uint8_t *out_data, size_t out_len,
                          uint8_t *in_data, size_t in_len) {
    if (js_ref < 0) return 1; // Error
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "writeto_then_readfrom", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t out_array = mp_obj_new_bytes(out_data, out_len);
            mp_obj_t args[3] = {
                mp_obj_new_int(address),
                out_array,
                mp_obj_new_int(in_len)
            };
            mp_obj_t result = mp_call_function_n_kw(method, 3, 0, args);
            
            // Extract data from result
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(result, &bufinfo, MP_BUFFER_READ);
            memcpy(in_data, bufinfo.buf, MIN(in_len, bufinfo.len));
            
            nlr_pop();
            return 0; // Success
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return 1; // Error
}

// CircuitPython I2C implementation
void common_hal_busio_i2c_construct(busio_i2c_obj_t *self,
                                   const mcu_pin_obj_t *scl, const mcu_pin_obj_t *sda,
                                   uint32_t frequency, uint32_t timeout) {
    claim_pin(scl);
    claim_pin(sda);
    
    self->scl_pin = scl;
    self->sda_pin = sda;
    self->frequency = frequency;
    self->has_lock = false;
    
    // Create JavaScript-backed I2C
    if (!js_i2c_create(scl, sda, frequency, &self->js_i2c_ref)) {
        self->js_i2c_ref = -1;
        mp_raise_RuntimeError(MP_ERROR_TEXT("Could not create JavaScript I2C backend"));
    }
}

bool common_hal_busio_i2c_deinited(busio_i2c_obj_t *self) {
    return self->scl_pin == NULL;
}

void common_hal_busio_i2c_deinit(busio_i2c_obj_t *self) {
    if (common_hal_busio_i2c_deinited(self)) {
        return;
    }
    
    js_i2c_deinit(self->js_i2c_ref);
    reset_pin_number(0, self->scl_pin->number);
    reset_pin_number(0, self->sda_pin->number);
    self->scl_pin = NULL;
    self->sda_pin = NULL;
    self->js_i2c_ref = -1;
}

bool common_hal_busio_i2c_probe(busio_i2c_obj_t *self, uint8_t addr) {
    return js_i2c_probe_for_device(self->js_i2c_ref, addr) == 0;
}

bool common_hal_busio_i2c_try_lock(busio_i2c_obj_t *self) {
    bool success = js_i2c_try_lock(self->js_i2c_ref);
    self->has_lock = success;
    return success;
}

bool common_hal_busio_i2c_has_lock(busio_i2c_obj_t *self) {
    return self->has_lock && js_i2c_has_lock(self->js_i2c_ref);
}

void common_hal_busio_i2c_unlock(busio_i2c_obj_t *self) {
    self->has_lock = false;
    js_i2c_unlock(self->js_i2c_ref);
}

uint8_t common_hal_busio_i2c_write(busio_i2c_obj_t *self, uint16_t address,
                                   const uint8_t *data, size_t len) {
    return js_i2c_write(self->js_i2c_ref, address, data, len, true); // Always send stop bit
}

uint8_t common_hal_busio_i2c_read(busio_i2c_obj_t *self, uint16_t address,
                                  uint8_t *data, size_t len) {
    return js_i2c_read(self->js_i2c_ref, address, data, len);
}

uint8_t common_hal_busio_i2c_write_read(busio_i2c_obj_t *self, uint16_t address,
                                       uint8_t *out_data, size_t out_len, uint8_t *in_data, size_t in_len) {
    return js_i2c_write_read(self->js_i2c_ref, address, out_data, out_len, in_data, in_len);
}

void common_hal_busio_i2c_never_reset(busio_i2c_obj_t *self) {
    never_reset_pin_number(0, self->scl_pin->number);
    never_reset_pin_number(0, self->sda_pin->number);
}