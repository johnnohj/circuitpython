// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

// JavaScript-backed SPI object
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *clock_pin;
    const mcu_pin_obj_t *mosi_pin;
    const mcu_pin_obj_t *miso_pin;
    int js_spi_ref;  // Reference to JavaScript SPI object
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    bool has_lock;
} busio_spi_obj_t;

// JavaScript semihosting functions
extern bool js_spi_create(const mcu_pin_obj_t *clock, const mcu_pin_obj_t *mosi, 
                          const mcu_pin_obj_t *miso, int *js_ref_out);
extern void js_spi_deinit(int js_ref);
extern void js_spi_configure(int js_ref, uint32_t baudrate, uint8_t polarity, 
                             uint8_t phase, uint8_t bits);
extern bool js_spi_try_lock(int js_ref);
extern bool js_spi_has_lock(int js_ref);
extern void js_spi_unlock(int js_ref);
extern bool js_spi_write(int js_ref, const uint8_t *data, size_t len);
extern bool js_spi_read(int js_ref, uint8_t *data, size_t len, uint8_t write_value);
extern bool js_spi_transfer(int js_ref, const uint8_t *write_data, uint8_t *read_data, size_t len);