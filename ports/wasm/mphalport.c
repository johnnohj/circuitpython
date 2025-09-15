// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython WebAssembly Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "py/misc.h"
#include <emscripten.h>

#include "py/obj.h" 
#include "py/mphal.h"
#include "py/gc.h"
#include "py/nlr.h"
#include "py/lexer.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/misc.h"
#include "mpconfigport.h"

#if CIRCUITPY_HAL_PROVIDER
#include "hal_provider.h"
#endif

// Control character constants
#define CHAR_CTRL_A (1)
#define CHAR_CTRL_B (2) 
#define CHAR_CTRL_C (3)
#define CHAR_CTRL_D (4)

// WebAssembly HAL port implementation

// mp_hal_stdout_tx_str is now provided by shared/runtime/stdout_helpers.c

// mp_hal_stdout_tx_strn_cooked is now provided by shared/runtime/stdout_helpers.c

uint32_t mp_hal_ticks_ms(void) {
    // Use Emscripten's time function
    return (uint32_t)emscripten_get_now();
}

mp_uint_t mp_hal_ticks_us(void) {
    // Convert milliseconds to microseconds 
    return (mp_uint_t)(emscripten_get_now() * 1000.0);
}

void mp_hal_delay_ms(mp_uint_t ms) {
    // In WebAssembly, we can't actually delay
    // This would need to be handled by JavaScript
    (void)ms;
}

void mp_hal_delay_us(mp_uint_t us) {
    // In WebAssembly, we can't actually delay
    (void)us;
}

#if CIRCUITPY_HAL_PROVIDER
// HAL-based pin operations (if enabled)

void mp_hal_pin_output(mp_hal_pin_obj_t pin) {
    const hal_provider_t *provider = hal_get_provider();
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_direction) {
        hal_pin_t *hal_pin = (hal_pin_t*)pin;
        provider->pin_ops->digital_set_direction(hal_pin, true);
    }
}

void mp_hal_pin_input(mp_hal_pin_obj_t pin) {
    const hal_provider_t *provider = hal_get_provider();
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_direction) {
        hal_pin_t *hal_pin = (hal_pin_t*)pin;
        provider->pin_ops->digital_set_direction(hal_pin, false);
    }
}

void mp_hal_pin_high(mp_hal_pin_obj_t pin) {
    const hal_provider_t *provider = hal_get_provider();
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_value) {
        hal_pin_t *hal_pin = (hal_pin_t*)pin;
        provider->pin_ops->digital_set_value(hal_pin, true);
    }
}

void mp_hal_pin_low(mp_hal_pin_obj_t pin) {
    const hal_provider_t *provider = hal_get_provider();
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_value) {
        hal_pin_t *hal_pin = (hal_pin_t*)pin;
        provider->pin_ops->digital_set_value(hal_pin, false);
    }
}

bool mp_hal_pin_read(mp_hal_pin_obj_t pin) {
    const hal_provider_t *provider = hal_get_provider();
    if (provider && provider->pin_ops && provider->pin_ops->digital_get_value) {
        hal_pin_t *hal_pin = (hal_pin_t*)pin;
        return provider->pin_ops->digital_get_value(hal_pin);
    }
    return false;
}

void mp_hal_pin_write(mp_hal_pin_obj_t pin, bool value) {
    if (value) {
        mp_hal_pin_high(pin);
    } else {
        mp_hal_pin_low(pin);
    }
}

#else
// Stub implementations when HAL is not enabled

void mp_hal_pin_output(mp_hal_pin_obj_t pin) { (void)pin; }
void mp_hal_pin_input(mp_hal_pin_obj_t pin) { (void)pin; }  
void mp_hal_pin_high(mp_hal_pin_obj_t pin) { (void)pin; }
void mp_hal_pin_low(mp_hal_pin_obj_t pin) { (void)pin; }
bool mp_hal_pin_read(mp_hal_pin_obj_t pin) { (void)pin; return false; }
void mp_hal_pin_write(mp_hal_pin_obj_t pin, bool value) { (void)pin; (void)value; }

#endif

// Missing MicroPython functions  
void nlr_jump_fail(void *val) {
    printf("FATAL: uncaught exception\n");
    emscripten_force_exit(1);
}

void gc_collect(void) {
    // Basic GC collection - call the MicroPython GC properly
    gc_collect_start();
    gc_collect_end();
}

// Stubs for missing symbols needed by the REPL
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    return NULL; // Not supported in WebAssembly
}

mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST; // No file system in WebAssembly
}

// The real readline implementation is in shared/readline/readline.c
// No stub needed - the shared implementation handles everything

// System objects stubs - only define what's needed
const mp_obj_type_t mp_type_ringio = { 0 };
mp_obj_t mp_sys_stdin_obj = MP_OBJ_NULL;
mp_obj_t mp_sys_stdout_obj = MP_OBJ_NULL;
mp_obj_t mp_sys_stderr_obj = MP_OBJ_NULL;

// Builtin open stub (needed when VFS is disabled)
mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    mp_raise_OSError(ENOENT); // No file system support
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

// Missing module stubs - forward declare globals first
const mp_obj_dict_t mp_module_uctypes_globals;
const mp_obj_module_t mp_module_uctypes = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_uctypes_globals,
};
const mp_obj_dict_t mp_module_uctypes_globals = {
    .base = {&mp_type_dict},
    .map = {
        .all_keys_are_qstrs = 1,
        .is_fixed = 1,
        .is_ordered = 1,
        .used = 0,
        .alloc = 0,
        .table = NULL,
    },
};

