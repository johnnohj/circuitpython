// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port default board - 64 GPIO pins, all fully capable

#include "shared-bindings/board/__init__.h"
#include "shared-bindings/microcontroller/Pin.h"

// External pin objects (from Pin.c)
extern mcu_pin_obj_t pin_GPIO0;
extern mcu_pin_obj_t pin_GPIO1;
extern mcu_pin_obj_t pin_GPIO2;
extern mcu_pin_obj_t pin_GPIO3;
extern mcu_pin_obj_t pin_GPIO4;
extern mcu_pin_obj_t pin_GPIO5;
extern mcu_pin_obj_t pin_GPIO6;
extern mcu_pin_obj_t pin_GPIO7;
extern mcu_pin_obj_t pin_GPIO8;
extern mcu_pin_obj_t pin_GPIO9;
extern mcu_pin_obj_t pin_GPIO10;
extern mcu_pin_obj_t pin_GPIO11;
extern mcu_pin_obj_t pin_GPIO12;
extern mcu_pin_obj_t pin_GPIO13;
extern mcu_pin_obj_t pin_GPIO14;
extern mcu_pin_obj_t pin_GPIO15;
extern mcu_pin_obj_t pin_GPIO16;
extern mcu_pin_obj_t pin_GPIO17;
extern mcu_pin_obj_t pin_GPIO18;
extern mcu_pin_obj_t pin_GPIO19;
extern mcu_pin_obj_t pin_GPIO20;
extern mcu_pin_obj_t pin_GPIO21;
extern mcu_pin_obj_t pin_GPIO22;
extern mcu_pin_obj_t pin_GPIO23;
extern mcu_pin_obj_t pin_GPIO24;
extern mcu_pin_obj_t pin_GPIO25;
extern mcu_pin_obj_t pin_GPIO26;
extern mcu_pin_obj_t pin_GPIO27;
extern mcu_pin_obj_t pin_GPIO28;
extern mcu_pin_obj_t pin_GPIO29;
extern mcu_pin_obj_t pin_GPIO30;
extern mcu_pin_obj_t pin_GPIO31;
extern mcu_pin_obj_t pin_GPIO32;
extern mcu_pin_obj_t pin_GPIO33;
extern mcu_pin_obj_t pin_GPIO34;
extern mcu_pin_obj_t pin_GPIO35;
extern mcu_pin_obj_t pin_GPIO36;
extern mcu_pin_obj_t pin_GPIO37;
extern mcu_pin_obj_t pin_GPIO38;
extern mcu_pin_obj_t pin_GPIO39;
extern mcu_pin_obj_t pin_GPIO40;
extern mcu_pin_obj_t pin_GPIO41;
extern mcu_pin_obj_t pin_GPIO42;
extern mcu_pin_obj_t pin_GPIO43;
extern mcu_pin_obj_t pin_GPIO44;
extern mcu_pin_obj_t pin_GPIO45;
extern mcu_pin_obj_t pin_GPIO46;
extern mcu_pin_obj_t pin_GPIO47;
extern mcu_pin_obj_t pin_GPIO48;
extern mcu_pin_obj_t pin_GPIO49;
extern mcu_pin_obj_t pin_GPIO50;
extern mcu_pin_obj_t pin_GPIO51;
extern mcu_pin_obj_t pin_GPIO52;
extern mcu_pin_obj_t pin_GPIO53;
extern mcu_pin_obj_t pin_GPIO54;
extern mcu_pin_obj_t pin_GPIO55;
extern mcu_pin_obj_t pin_GPIO56;
extern mcu_pin_obj_t pin_GPIO57;
extern mcu_pin_obj_t pin_GPIO58;
extern mcu_pin_obj_t pin_GPIO59;
extern mcu_pin_obj_t pin_GPIO60;
extern mcu_pin_obj_t pin_GPIO61;
extern mcu_pin_obj_t pin_GPIO62;
extern mcu_pin_obj_t pin_GPIO63;

// Default WASM CircuitPython Board
// Provides a generic layout compatible with common CircuitPython code
static const mp_rom_map_elem_t board_module_globals_table[] = {
    CIRCUITPYTHON_BOARD_DICT_STANDARD_ITEMS

    // Digital pins D0-D63 (all GPIO pins)
    { MP_ROM_QSTR(MP_QSTR_D0), MP_ROM_PTR(&pin_GPIO0) },
    { MP_ROM_QSTR(MP_QSTR_D1), MP_ROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_D2), MP_ROM_PTR(&pin_GPIO2) },
    { MP_ROM_QSTR(MP_QSTR_D3), MP_ROM_PTR(&pin_GPIO3) },
    { MP_ROM_QSTR(MP_QSTR_D4), MP_ROM_PTR(&pin_GPIO4) },
    { MP_ROM_QSTR(MP_QSTR_D5), MP_ROM_PTR(&pin_GPIO5) },
    { MP_ROM_QSTR(MP_QSTR_D6), MP_ROM_PTR(&pin_GPIO6) },
    { MP_ROM_QSTR(MP_QSTR_D7), MP_ROM_PTR(&pin_GPIO7) },
    { MP_ROM_QSTR(MP_QSTR_D8), MP_ROM_PTR(&pin_GPIO8) },
    { MP_ROM_QSTR(MP_QSTR_D9), MP_ROM_PTR(&pin_GPIO9) },
    { MP_ROM_QSTR(MP_QSTR_D10), MP_ROM_PTR(&pin_GPIO10) },
    { MP_ROM_QSTR(MP_QSTR_D11), MP_ROM_PTR(&pin_GPIO11) },
    { MP_ROM_QSTR(MP_QSTR_D12), MP_ROM_PTR(&pin_GPIO12) },
    { MP_ROM_QSTR(MP_QSTR_D13), MP_ROM_PTR(&pin_GPIO13) },
    { MP_ROM_QSTR(MP_QSTR_D14), MP_ROM_PTR(&pin_GPIO14) },
    { MP_ROM_QSTR(MP_QSTR_D15), MP_ROM_PTR(&pin_GPIO15) },
    { MP_ROM_QSTR(MP_QSTR_D16), MP_ROM_PTR(&pin_GPIO16) },
    { MP_ROM_QSTR(MP_QSTR_D17), MP_ROM_PTR(&pin_GPIO17) },
    { MP_ROM_QSTR(MP_QSTR_D18), MP_ROM_PTR(&pin_GPIO18) },
    { MP_ROM_QSTR(MP_QSTR_D19), MP_ROM_PTR(&pin_GPIO19) },
    { MP_ROM_QSTR(MP_QSTR_D20), MP_ROM_PTR(&pin_GPIO20) },
    { MP_ROM_QSTR(MP_QSTR_D21), MP_ROM_PTR(&pin_GPIO21) },
    { MP_ROM_QSTR(MP_QSTR_D22), MP_ROM_PTR(&pin_GPIO22) },
    { MP_ROM_QSTR(MP_QSTR_D23), MP_ROM_PTR(&pin_GPIO23) },
    { MP_ROM_QSTR(MP_QSTR_D24), MP_ROM_PTR(&pin_GPIO24) },
    { MP_ROM_QSTR(MP_QSTR_D25), MP_ROM_PTR(&pin_GPIO25) },
    { MP_ROM_QSTR(MP_QSTR_D26), MP_ROM_PTR(&pin_GPIO26) },
    { MP_ROM_QSTR(MP_QSTR_D27), MP_ROM_PTR(&pin_GPIO27) },
    { MP_ROM_QSTR(MP_QSTR_D28), MP_ROM_PTR(&pin_GPIO28) },
    { MP_ROM_QSTR(MP_QSTR_D29), MP_ROM_PTR(&pin_GPIO29) },
    { MP_ROM_QSTR(MP_QSTR_D30), MP_ROM_PTR(&pin_GPIO30) },
    { MP_ROM_QSTR(MP_QSTR_D31), MP_ROM_PTR(&pin_GPIO31) },
    { MP_ROM_QSTR(MP_QSTR_D32), MP_ROM_PTR(&pin_GPIO32) },
    { MP_ROM_QSTR(MP_QSTR_D33), MP_ROM_PTR(&pin_GPIO33) },
    { MP_ROM_QSTR(MP_QSTR_D34), MP_ROM_PTR(&pin_GPIO34) },
    { MP_ROM_QSTR(MP_QSTR_D35), MP_ROM_PTR(&pin_GPIO35) },
    { MP_ROM_QSTR(MP_QSTR_D36), MP_ROM_PTR(&pin_GPIO36) },
    { MP_ROM_QSTR(MP_QSTR_D37), MP_ROM_PTR(&pin_GPIO37) },
    { MP_ROM_QSTR(MP_QSTR_D38), MP_ROM_PTR(&pin_GPIO38) },
    { MP_ROM_QSTR(MP_QSTR_D39), MP_ROM_PTR(&pin_GPIO39) },
    { MP_ROM_QSTR(MP_QSTR_D40), MP_ROM_PTR(&pin_GPIO40) },
    { MP_ROM_QSTR(MP_QSTR_D41), MP_ROM_PTR(&pin_GPIO41) },
    { MP_ROM_QSTR(MP_QSTR_D42), MP_ROM_PTR(&pin_GPIO42) },
    { MP_ROM_QSTR(MP_QSTR_D43), MP_ROM_PTR(&pin_GPIO43) },
    { MP_ROM_QSTR(MP_QSTR_D44), MP_ROM_PTR(&pin_GPIO44) },
    { MP_ROM_QSTR(MP_QSTR_D45), MP_ROM_PTR(&pin_GPIO45) },
    { MP_ROM_QSTR(MP_QSTR_D46), MP_ROM_PTR(&pin_GPIO46) },
    { MP_ROM_QSTR(MP_QSTR_D47), MP_ROM_PTR(&pin_GPIO47) },
    { MP_ROM_QSTR(MP_QSTR_D48), MP_ROM_PTR(&pin_GPIO48) },
    { MP_ROM_QSTR(MP_QSTR_D49), MP_ROM_PTR(&pin_GPIO49) },
    { MP_ROM_QSTR(MP_QSTR_D50), MP_ROM_PTR(&pin_GPIO50) },
    { MP_ROM_QSTR(MP_QSTR_D51), MP_ROM_PTR(&pin_GPIO51) },
    { MP_ROM_QSTR(MP_QSTR_D52), MP_ROM_PTR(&pin_GPIO52) },
    { MP_ROM_QSTR(MP_QSTR_D53), MP_ROM_PTR(&pin_GPIO53) },
    { MP_ROM_QSTR(MP_QSTR_D54), MP_ROM_PTR(&pin_GPIO54) },
    { MP_ROM_QSTR(MP_QSTR_D55), MP_ROM_PTR(&pin_GPIO55) },
    { MP_ROM_QSTR(MP_QSTR_D56), MP_ROM_PTR(&pin_GPIO56) },
    { MP_ROM_QSTR(MP_QSTR_D57), MP_ROM_PTR(&pin_GPIO57) },
    { MP_ROM_QSTR(MP_QSTR_D58), MP_ROM_PTR(&pin_GPIO58) },
    { MP_ROM_QSTR(MP_QSTR_D59), MP_ROM_PTR(&pin_GPIO59) },
    { MP_ROM_QSTR(MP_QSTR_D60), MP_ROM_PTR(&pin_GPIO60) },
    { MP_ROM_QSTR(MP_QSTR_D61), MP_ROM_PTR(&pin_GPIO61) },
    { MP_ROM_QSTR(MP_QSTR_D62), MP_ROM_PTR(&pin_GPIO62) },
    { MP_ROM_QSTR(MP_QSTR_D63), MP_ROM_PTR(&pin_GPIO63) },

    // Analog pins A0-A7 (mapped to first 8 GPIO pins)
    { MP_ROM_QSTR(MP_QSTR_A0), MP_ROM_PTR(&pin_GPIO0) },
    { MP_ROM_QSTR(MP_QSTR_A1), MP_ROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_A2), MP_ROM_PTR(&pin_GPIO2) },
    { MP_ROM_QSTR(MP_QSTR_A3), MP_ROM_PTR(&pin_GPIO3) },
    { MP_ROM_QSTR(MP_QSTR_A4), MP_ROM_PTR(&pin_GPIO4) },
    { MP_ROM_QSTR(MP_QSTR_A5), MP_ROM_PTR(&pin_GPIO5) },
    { MP_ROM_QSTR(MP_QSTR_A6), MP_ROM_PTR(&pin_GPIO6) },
    { MP_ROM_QSTR(MP_QSTR_A7), MP_ROM_PTR(&pin_GPIO7) },

    // I2C default pins
    { MP_ROM_QSTR(MP_QSTR_SDA), MP_ROM_PTR(&pin_GPIO8) },
    { MP_ROM_QSTR(MP_QSTR_SCL), MP_ROM_PTR(&pin_GPIO9) },

    // SPI default pins
    { MP_ROM_QSTR(MP_QSTR_MOSI), MP_ROM_PTR(&pin_GPIO10) },
    { MP_ROM_QSTR(MP_QSTR_MISO), MP_ROM_PTR(&pin_GPIO11) },
    { MP_ROM_QSTR(MP_QSTR_SCK), MP_ROM_PTR(&pin_GPIO12) },

    // UART default pins
    { MP_ROM_QSTR(MP_QSTR_TX), MP_ROM_PTR(&pin_GPIO14) },
    { MP_ROM_QSTR(MP_QSTR_RX), MP_ROM_PTR(&pin_GPIO15) },

    // Special pins
    { MP_ROM_QSTR(MP_QSTR_LED), MP_ROM_PTR(&pin_GPIO13) },
    { MP_ROM_QSTR(MP_QSTR_NEOPIXEL), MP_ROM_PTR(&pin_GPIO16) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON), MP_ROM_PTR(&pin_GPIO17) },
};

MP_DEFINE_CONST_DICT(board_module_globals, board_module_globals_table);
