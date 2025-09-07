// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

// WebAssembly port board configuration

#define MICROPY_HW_BOARD_NAME "CircuitPython WebAssembly Port"
#define MICROPY_HW_MCU_NAME "Emscripten"

#define CIRCUITPY_BOARD_ID "circuitpy_webassembly"

// Board-specific definitions for CircuitPython
#define BOARD_HAS_CRYSTAL 1

// Default pin definitions (can be overridden by JavaScript semihosting)
#define DEFAULT_UART_BUS_TX_PIN (&pin_CONSOLE_TX)
#define DEFAULT_UART_BUS_RX_PIN (&pin_CONSOLE_RX)

// WebAssembly specific features
#define CIRCUITPY_BOARD_DICT_STANDARD_ITEMS \
    { MP_ROM_QSTR(MP_QSTR_board_id), MP_ROM_QSTR(MP_QSTR_circuitpy_webassembly) }, \
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_board) },