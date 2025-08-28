/*
 * This file is part of the CircuitPython project, https://github.com/adafruit/circuitpython
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 CircuitPython Contributors
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
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// WebAssembly stubs for CircuitPython supervisor functionality

#include <stdio.h>
#include "py/runtime.h"
#include "supervisor/port.h"
#include "supervisor/board.h"
#include "supervisor/shared/safe_mode.h"

// Supervisor port functions required by CircuitPython
void port_background_tick(void) {
    // No background tasks needed for WebAssembly
}

void port_start_background_tick(void) {
    // No background tasks needed for WebAssembly
}

void port_finish_background_tick(void) {
    // No background tasks needed for WebAssembly  
}

void port_wake_main_task(void) {
    // No tasks to wake in WebAssembly
}

void port_yield(void) {
    // No yielding needed in WebAssembly
}

void port_boot_info(void) {
    // No boot info to display for WebAssembly
}

void port_heap_print_info(void) {
    // Use default heap info
}

uint32_t port_get_saved_word(void) {
    return 0; // No saved words in WebAssembly
}

void port_set_saved_word(uint32_t value) {
    // No saved words in WebAssembly
    (void)value;
}

// Safe mode functionality
void port_set_saved_safe_mode(safe_mode_t reason) {
    // Safe mode not implemented for WebAssembly
    (void)reason;
}

safe_mode_t port_get_saved_safe_mode(void) {
    return SAFE_MODE_NONE;
}

// Note: Board functions are provided by board.c

// Reset functionality
void port_reset(void) {
    // No reset functionality for WebAssembly
}

void port_get_serial_number(char *str, size_t len) {
    // Generate a simple serial number for WebAssembly
    snprintf(str, len, "WEBASM%08X", (unsigned int)(uintptr_t)&str);
}