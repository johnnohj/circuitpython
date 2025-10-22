// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - UART stub (not yet implemented)

#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *tx;
    const mcu_pin_obj_t *rx;
    uint32_t baudrate;
    uint8_t character_bits;
    bool rx_ongoing;
    uint32_t timeout_ms;
} busio_uart_obj_t;
