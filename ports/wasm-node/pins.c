// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include "mpconfigboard.h"
#include "shared-bindings/board/__init__.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "hal_provider.h"
#include "generic_board.h"

// HAL-based board module for unified architecture
static mp_obj_dict_t *hal_board_module_dict = NULL;
static bool hal_pins_initialized = false;

// Board module items with HAL provider pins
static mp_rom_map_elem_t hal_board_module_globals_table[64];  // Support up to 64 pins
static size_t hal_board_globals_count = 0;

// Initialize board pins from HAL provider
void hal_board_init_pins(void) {
    if (hal_pins_initialized) {
        return;
    }

    // Add standard board items
    hal_board_module_globals_table[hal_board_globals_count++] =
        (mp_rom_map_elem_t){ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_board) };
    hal_board_module_globals_table[hal_board_globals_count++] =
        (mp_rom_map_elem_t){ MP_ROM_QSTR(MP_QSTR_board_id), MP_ROM_PTR(&board_module_id_obj) };

    // Get pins from HAL provider
    const hal_provider_t *provider = hal_get_provider();
    if (provider != NULL) {
        // Add generic board pins from HAL
        for (int i = 0; i < GENERIC_BOARD_PIN_COUNT; i++) {
            hal_pin_t *pin = hal_pin_find_by_name(generic_board_pins[i].name);
            if (pin != NULL) {
                qstr pin_qstr = qstr_from_str(generic_board_pins[i].name);
                hal_board_module_globals_table[hal_board_globals_count++] =
                    (mp_rom_map_elem_t){ MP_ROM_QSTR(pin_qstr), MP_ROM_PTR(pin) };
            }
        }
    }

    hal_pins_initialized = true;

    // Update the board module globals used count
    ((mp_obj_dict_t*)&board_module_globals)->map.used = hal_board_globals_count;
}

// Get the board module dictionary (called by CircuitPython core)
const mp_obj_dict_t *get_board_module_dict(void) {
    hal_board_init_pins();

    if (hal_board_module_dict == NULL) {
        hal_board_module_dict = m_new_obj(mp_obj_dict_t);
        hal_board_module_dict->base.type = &mp_type_dict;
        mp_map_init(&hal_board_module_dict->map, hal_board_globals_count);

        // Add all the elements manually
        for (size_t i = 0; i < hal_board_globals_count; i++) {
            mp_map_lookup(&hal_board_module_dict->map,
                         (mp_obj_t)hal_board_module_globals_table[i].key,
                         MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value =
                         (mp_obj_t)hal_board_module_globals_table[i].value;
        }
    }

    return hal_board_module_dict;
}

// Override the board module globals to use our HAL-based pins
const mp_obj_dict_t board_module_globals = {
    .base = {&mp_type_dict},
    .map = {
        .all_keys_are_qstrs = 1,
        .is_fixed = 1,
        .is_ordered = 1,
        .used = 0,  // Will be set dynamically
        .alloc = 64,  // Max pins
        .table = (mp_map_elem_t*)hal_board_module_globals_table,
    },
};