// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include "mpconfigboard.h"
#include "shared-bindings/board/__init__.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "proxy_c.h"

// Dynamic board module for JavaScript semihosting
static mp_obj_dict_t *js_board_module_dict = NULL;
static bool js_pins_initialized = false;

// Standard board module items when no JavaScript pins are provided
static const mp_rom_map_elem_t static_board_module_globals_table[] = {
    CIRCUITPYTHON_BOARD_DICT_STANDARD_ITEMS
    
    // WebAssembly can optionally include virtual pins for testing
    #ifdef CIRCUITPY_INCLUDE_VIRTUAL_PINS
    { MP_ROM_QSTR(MP_QSTR_VIRTUAL_LED), MP_ROM_PTR(&pin_VIRTUAL_LED) },
    { MP_ROM_QSTR(MP_QSTR_VIRTUAL_BUTTON), MP_ROM_PTR(&pin_VIRTUAL_BUTTON) },
    #endif
};

// JavaScript API function to register board pins
void mp_js_register_board_pins(uint32_t *pins_array, size_t num_pins) {
    if (js_board_module_dict == NULL) {
        js_board_module_dict = mp_obj_new_dict(num_pins + 5);
        
        // Add standard items
        mp_obj_dict_store(js_board_module_dict, 
                         MP_ROM_QSTR(MP_QSTR___name__), 
                         MP_ROM_QSTR(MP_QSTR_board));
        
        // Add board_id 
        mp_obj_dict_store(js_board_module_dict,
                         MP_ROM_QSTR(MP_QSTR_board_id),
                         MP_OBJ_NEW_QSTR(MP_QSTR_webassembly));
    }
    
    // Add JavaScript-defined pins
    for (size_t i = 0; i < num_pins; i++) {
        uint32_t *pin_def = &pins_array[i * 4]; // [name_qstr, js_ref, pin_number, capabilities]
        qstr pin_name = pin_def[0];
        uint8_t pin_number = pin_def[2];
        uint32_t capabilities = pin_def[3];
        
        mp_obj_t pin_obj = mp_js_create_pin(&pin_def[1], pin_number, capabilities);
        mp_obj_dict_store(js_board_module_dict, MP_OBJ_NEW_QSTR(pin_name), pin_obj);
    }
    
    js_pins_initialized = true;
}

// Return the appropriate board module dictionary
MP_DEFINE_CONST_DICT(board_module_globals, static_board_module_globals_table);