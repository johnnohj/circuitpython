// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// JavaScript-backed I2C object
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *scl_pin;
    const mcu_pin_obj_t *sda_pin;
    int js_i2c_ref;  // Reference to JavaScript I2C object
    uint32_t frequency;
    bool has_lock;
} busio_i2c_obj_t;

// JavaScript semihosting functions
extern bool js_i2c_create(const mcu_pin_obj_t *scl_pin, const mcu_pin_obj_t *sda_pin, 
                          uint32_t frequency, int *js_ref_out);
extern void js_i2c_deinit(int js_ref);
extern bool js_i2c_try_lock(int js_ref);
extern bool js_i2c_has_lock(int js_ref);
extern void js_i2c_unlock(int js_ref);
extern uint8_t js_i2c_probe_for_device(int js_ref, uint8_t address);
extern uint8_t js_i2c_write(int js_ref, uint16_t address, const uint8_t *data, size_t len, bool stop);
extern uint8_t js_i2c_read(int js_ref, uint16_t address, uint8_t *data, size_t len);
extern uint8_t js_i2c_write_read(int js_ref, uint16_t address, 
                                 const uint8_t *out_data, size_t out_len,
                                 uint8_t *in_data, size_t in_len);