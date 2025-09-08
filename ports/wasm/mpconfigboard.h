// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython WebAssembly Contributors
//
// SPDX-License-Identifier: MIT

// Board configuration for CircuitPython WebAssembly HAL Port

#define MICROPY_HW_BOARD_NAME "CTPY_WASM"
#define MICROPY_HW_MCU_NAME "Emscripten"

// No real board features for WebAssembly
#define BOARD_NO_VBUS_SENSE (1)
#define BOARD_NO_USB_OTG_ID_SENSE (1)

// Default board configuration for CircuitPython
#define DEFAULT_I2C_BUS_SCL (&pin_GPIO0)  // Placeholder
#define DEFAULT_I2C_BUS_SDA (&pin_GPIO1)  // Placeholder
#define DEFAULT_SPI_BUS_SCK (&pin_GPIO2)  // Placeholder
#define DEFAULT_SPI_BUS_MOSI (&pin_GPIO3) // Placeholder
#define DEFAULT_SPI_BUS_MISO (&pin_GPIO4) // Placeholder
#define DEFAULT_UART_BUS_RX (&pin_GPIO5)  // Placeholder
#define DEFAULT_UART_BUS_TX (&pin_GPIO6)  // Placeholder

// Mock pin objects for compilation
extern mp_obj_t pin_GPIO0;
extern mp_obj_t pin_GPIO1;
extern mp_obj_t pin_GPIO2;
extern mp_obj_t pin_GPIO3;
extern mp_obj_t pin_GPIO4;
extern mp_obj_t pin_GPIO5;
extern mp_obj_t pin_GPIO6;
