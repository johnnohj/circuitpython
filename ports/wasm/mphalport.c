// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython WebAssembly Contributors
//
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <emscripten.h>

#include "py/obj.h" 
#include "py/mphal.h"
#include "py/gc.h"
#include "py/nlr.h"
#include "mpconfigport.h"

#if CIRCUITPY_HAL_PROVIDER
#include "hal_provider.h"
#endif

// WebAssembly HAL port implementation

void mp_hal_set_interrupt_char(int c) {
    // Not implemented for WebAssembly
}

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    // For cooked output, we can process newlines if needed
    mp_hal_stdout_tx_strn(str, len);
}

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

bool mp_hal_is_interrupted(void) {
    return false;
}