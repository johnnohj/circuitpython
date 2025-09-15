// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"

#include "common-hal/busio/SPI.h"
#include "shared-bindings/busio/SPI.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "proxy_c.h"

// JavaScript semihosting functions for SPI (similar to I2C pattern)
bool js_spi_create(const mcu_pin_obj_t *clock, const mcu_pin_obj_t *mosi, 
                   const mcu_pin_obj_t *miso, int *js_ref_out) {
    uint32_t out[3];
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        proxy_c_to_js_lookup_attr(0, "createSPI", out); // Global function
        mp_obj_t create_method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(create_method)) {
            mp_obj_t args[3] = {
                mp_obj_new_int(clock->number),
                mosi ? mp_obj_new_int(mosi->number) : mp_const_none,
                miso ? mp_obj_new_int(miso->number) : mp_const_none
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

void js_spi_deinit(int js_ref) {
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

void js_spi_configure(int js_ref, uint32_t baudrate, uint8_t polarity, 
                      uint8_t phase, uint8_t bits) {
    if (js_ref < 0) return;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "configure", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t args[4] = {
                mp_obj_new_int(baudrate),
                mp_obj_new_int(polarity),
                mp_obj_new_int(phase),
                mp_obj_new_int(bits)
            };
            mp_call_function_n_kw(method, 4, 0, args);
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
}

bool js_spi_try_lock(int js_ref) {
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

bool js_spi_has_lock(int js_ref) {
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

void js_spi_unlock(int js_ref) {
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

bool js_spi_write(int js_ref, const uint8_t *data, size_t len) {
    if (js_ref < 0) return false;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "write", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t data_array = mp_obj_new_bytes(data, len);
            mp_call_function_1(method, data_array);
            nlr_pop();
            return true;
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return false;
}

bool js_spi_read(int js_ref, uint8_t *data, size_t len, uint8_t write_value) {
    if (js_ref < 0) return false;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "readinto", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t args[2] = {
                mp_obj_new_int(len),
                mp_obj_new_int(write_value)
            };
            mp_obj_t result = mp_call_function_n_kw(method, 2, 0, args);
            
            // Extract data from result
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(result, &bufinfo, MP_BUFFER_READ);
            memcpy(data, bufinfo.buf, MIN(len, bufinfo.len));
            
            nlr_pop();
            return true;
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return false;
}

bool js_spi_transfer(int js_ref, const uint8_t *write_data, uint8_t *read_data, size_t len) {
    if (js_ref < 0) return false;
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        uint32_t out[3];
        proxy_c_to_js_lookup_attr(js_ref, "write_readinto", out);
        mp_obj_t method = proxy_convert_js_to_mp_obj_cside(out);
        
        if (mp_obj_is_callable(method)) {
            mp_obj_t write_array = mp_obj_new_bytes(write_data, len);
            mp_obj_t result = mp_call_function_1(method, write_array);
            
            // Extract read data from result
            mp_buffer_info_t bufinfo;
            mp_get_buffer_raise(result, &bufinfo, MP_BUFFER_READ);
            memcpy(read_data, bufinfo.buf, MIN(len, bufinfo.len));
            
            nlr_pop();
            return true;
        }
        nlr_pop();
    } else {
        nlr_pop();
    }
    
    return false;
}

// CircuitPython SPI implementation
void common_hal_busio_spi_construct(busio_spi_obj_t *self,
                                   const mcu_pin_obj_t *clock, const mcu_pin_obj_t *mosi,
                                   const mcu_pin_obj_t *miso, bool half_duplex) {
    claim_pin(clock);
    if (mosi) claim_pin(mosi);
    if (miso) claim_pin(miso);
    
    self->clock_pin = clock;
    self->mosi_pin = mosi;
    self->miso_pin = miso;
    self->baudrate = 100000; // Default 100kHz
    self->polarity = 0;
    self->phase = 0;
    self->bits = 8;
    self->has_lock = false;
    
    // Create JavaScript-backed SPI
    if (!js_spi_create(clock, mosi, miso, &self->js_spi_ref)) {
        self->js_spi_ref = -1;
        mp_raise_RuntimeError(MP_ERROR_TEXT("Could not create JavaScript SPI backend"));
    }
}

bool common_hal_busio_spi_deinited(busio_spi_obj_t *self) {
    return self->clock_pin == NULL;
}

void common_hal_busio_spi_deinit(busio_spi_obj_t *self) {
    if (common_hal_busio_spi_deinited(self)) {
        return;
    }
    
    js_spi_deinit(self->js_spi_ref);
    reset_pin_number(0, self->clock_pin->number);
    if (self->mosi_pin) reset_pin_number(0, self->mosi_pin->number);
    if (self->miso_pin) reset_pin_number(0, self->miso_pin->number);
    
    self->clock_pin = NULL;
    self->mosi_pin = NULL;
    self->miso_pin = NULL;
    self->js_spi_ref = -1;
}

bool common_hal_busio_spi_configure(busio_spi_obj_t *self, uint32_t baudrate,
                                   uint8_t polarity, uint8_t phase, uint8_t bits) {
    self->baudrate = baudrate;
    self->polarity = polarity;
    self->phase = phase;
    self->bits = bits;
    
    js_spi_configure(self->js_spi_ref, baudrate, polarity, phase, bits);
    return true;
}

bool common_hal_busio_spi_try_lock(busio_spi_obj_t *self) {
    bool success = js_spi_try_lock(self->js_spi_ref);
    self->has_lock = success;
    return success;
}

bool common_hal_busio_spi_has_lock(busio_spi_obj_t *self) {
    return self->has_lock && js_spi_has_lock(self->js_spi_ref);
}

void common_hal_busio_spi_unlock(busio_spi_obj_t *self) {
    self->has_lock = false;
    js_spi_unlock(self->js_spi_ref);
}

bool common_hal_busio_spi_write(busio_spi_obj_t *self, const uint8_t *data, size_t len) {
    return js_spi_write(self->js_spi_ref, data, len);
}

bool common_hal_busio_spi_read(busio_spi_obj_t *self, uint8_t *data, size_t len, uint8_t write_value) {
    return js_spi_read(self->js_spi_ref, data, len, write_value);
}

bool common_hal_busio_spi_transfer(busio_spi_obj_t *self, const uint8_t *write_data,
                                  uint8_t *read_data, size_t len) {
    return js_spi_transfer(self->js_spi_ref, write_data, read_data, len);
}

uint32_t common_hal_busio_spi_get_frequency(busio_spi_obj_t *self) {
    return self->baudrate;
}

uint8_t common_hal_busio_spi_get_phase(busio_spi_obj_t *self) {
    return self->phase;
}

uint8_t common_hal_busio_spi_get_polarity(busio_spi_obj_t *self) {
    return self->polarity;
}

void common_hal_busio_spi_never_reset(busio_spi_obj_t *self) {
    never_reset_pin_number(0, self->clock_pin->number);
    if (self->mosi_pin) never_reset_pin_number(0, self->mosi_pin->number);
    if (self->miso_pin) never_reset_pin_number(0, self->miso_pin->number);
}