/*
 * This file is part of the CircuitPython project, https://github.com/adafruit/circuitpython
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 CircuitPython Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "supervisor/board.h"
#include "py/runtime.h"
#include "proxy_c.h"

// WebAssembly board-specific implementation with JavaScript semihosting

// JavaScript board configuration object reference
static int js_board_config_ref = -1;
static bool js_semihosting_enabled = false;

// External safe proxy functions
extern bool proxy_c_is_initialized(void);
extern bool proxy_c_to_js_has_attr_safe(uint32_t c_ref, const char *attr_in);
extern void proxy_c_to_js_lookup_attr_safe(uint32_t c_ref, const char *attr_in, uint32_t *out);

bool board_requests_safe_mode(void) {
    // Check if JavaScript board configuration requests safe mode
    if (js_semihosting_enabled && js_board_config_ref >= 0 && proxy_c_is_initialized()) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            if (proxy_c_to_js_has_attr_safe(js_board_config_ref, "requestsSafeMode")) {
                uint32_t out[3];
                proxy_c_to_js_lookup_attr_safe(js_board_config_ref, "requestsSafeMode", out);
                mp_obj_t safe_mode_func = proxy_convert_js_to_mp_obj_cside(out);
                if (mp_obj_is_callable(safe_mode_func)) {
                    mp_obj_t result = mp_call_function_0(safe_mode_func);
                    nlr_pop();
                    return mp_obj_is_true(result);
                }
            }
            nlr_pop();
        } else {
            // Exception occurred, default to false
            nlr_pop();
        }
    }
    return false; // WebAssembly never requests safe mode by default
}

void board_init(void) {
    // WEBASM-FIX: Early return if proxy system not initialized
    // This prevents hanging during WebAssembly module initialization
    if (!proxy_c_is_initialized() || !js_semihosting_enabled || js_board_config_ref < 0) {
        return; // Skip proxy calls during initial initialization
    }
    
    // Check if JavaScript board configuration provides initialization
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        if (proxy_c_to_js_has_attr_safe(js_board_config_ref, "init")) {
            uint32_t out[3];
            proxy_c_to_js_lookup_attr_safe(js_board_config_ref, "init", out);
            mp_obj_t init_func = proxy_convert_js_to_mp_obj_cside(out);
            if (mp_obj_is_callable(init_func)) {
                mp_call_function_0(init_func);
            }
        }
        nlr_pop();
    } else {
        // Exception occurred, ignore and continue
        nlr_pop();
    }
}

void board_deinit(void) {
    // Check if JavaScript board configuration provides deinitialization
    if (proxy_c_is_initialized() && js_semihosting_enabled && js_board_config_ref >= 0) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            if (proxy_c_to_js_has_attr_safe(js_board_config_ref, "deinit")) {
                uint32_t out[3];
                proxy_c_to_js_lookup_attr_safe(js_board_config_ref, "deinit", out);
                mp_obj_t deinit_func = proxy_convert_js_to_mp_obj_cside(out);
                if (mp_obj_is_callable(deinit_func)) {
                    mp_call_function_0(deinit_func);
                }
            }
            nlr_pop();
        } else {
            // Exception occurred, ignore and continue
            nlr_pop();
        }
    }
}

bool board_pin_free_remaining_pins(void) {
    // Check if JavaScript board configuration provides pin management
    if (proxy_c_is_initialized() && js_semihosting_enabled && js_board_config_ref >= 0) {
        nlr_buf_t nlr;
        if (nlr_push(&nlr) == 0) {
            if (proxy_c_to_js_has_attr_safe(js_board_config_ref, "pinFreeRemaining")) {
                uint32_t out[3];
                proxy_c_to_js_lookup_attr_safe(js_board_config_ref, "pinFreeRemaining", out);
                mp_obj_t pin_free_func = proxy_convert_js_to_mp_obj_cside(out);
                if (mp_obj_is_callable(pin_free_func)) {
                    mp_obj_t result = mp_call_function_0(pin_free_func);
                    nlr_pop();
                    return mp_obj_is_true(result);
                }
            }
            nlr_pop();
        } else {
            // Exception occurred, default to true
            nlr_pop();
        }
    }
    return true; // No pins to free in WebAssembly by default
}

// JavaScript API function to register board configuration
void mp_js_register_board_config(uint32_t *js_config_ref) {
    js_board_config_ref = ((uint32_t*)js_config_ref)[1]; // Extract JS reference
    js_semihosting_enabled = true;
}