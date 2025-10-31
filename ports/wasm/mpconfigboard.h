// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM Virtual Board Configuration

#pragma once

#define MICROPY_HW_BOARD_NAME "WebAssembly"
#define MICROPY_HW_MCU_NAME "Emscripten"

// WASM virtual board has 64 GPIO pins (GPIO0-GPIO63)
// Pin definitions are handled dynamically in common-hal/microcontroller/Pin.c

// Default bus pin assignments for board.I2C(), board.SPI(), board.UART()
// These provide sensible defaults for the virtual hardware
#define DEFAULT_I2C_BUS_SDA         (&pin_GPIO0)
#define DEFAULT_I2C_BUS_SCL         (&pin_GPIO1)

#define DEFAULT_SPI_BUS_SCK         (&pin_GPIO2)
#define DEFAULT_SPI_BUS_MOSI        (&pin_GPIO3)
#define DEFAULT_SPI_BUS_MISO        (&pin_GPIO4)

#define DEFAULT_UART_BUS_TX         (&pin_GPIO5)
#define DEFAULT_UART_BUS_RX         (&pin_GPIO6)
