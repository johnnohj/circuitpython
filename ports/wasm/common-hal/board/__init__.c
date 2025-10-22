// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Stub board module for WASM port - no actual hardware pins

#include "shared-bindings/board/__init__.h"
#include "py/runtime.h"

// Stub implementations - WASM has no hardware buses

bool common_hal_board_is_i2c(mp_obj_t obj) {
    return false;
}

mp_obj_t common_hal_board_get_i2c(const mp_int_t instance) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("I2C not available on WASM"));
}

mp_obj_t common_hal_board_create_i2c(const mp_int_t instance) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("I2C not available on WASM"));
}

bool common_hal_board_is_spi(mp_obj_t obj) {
    return false;
}

mp_obj_t common_hal_board_get_spi(const mp_int_t instance) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("SPI not available on WASM"));
}

mp_obj_t common_hal_board_create_spi(const mp_int_t instance) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("SPI not available on WASM"));
}

bool common_hal_board_is_uart(mp_obj_t obj) {
    return false;
}

mp_obj_t common_hal_board_get_uart(const mp_int_t instance) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("UART not available on WASM"));
}

mp_obj_t common_hal_board_create_uart(const mp_int_t instance) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("UART not available on WASM"));
}
