/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * SPDX-FileCopyrightText: Copyright (c) 2013, 2014 Damien P. George
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

#include "py/mphal.h"
#include "py/runtime.h"
#include "shared-bindings/time/__init__.h"
#include "supervisor/shared/tick.h"
#include <emscripten.h>

// CIRCUITPY-CHANGE: WASM implementation using Emscripten with yielding support
// Uses emscripten_get_now() which returns milliseconds since epoch with high precision
// Delays use RUN_BACKGROUND_TASKS to yield control during sleep

uint64_t common_hal_time_monotonic_ms(void) {
    // emscripten_get_now() returns double milliseconds since epoch
    // This is monotonic enough for our purposes in WASM
    return (uint64_t)emscripten_get_now();
}

uint64_t common_hal_time_monotonic_ns(void) {
    // Convert milliseconds to nanoseconds
    // Note: We lose sub-millisecond precision here, but that's acceptable for WASM
    return common_hal_time_monotonic_ms() * 1000000ULL;
}

void common_hal_time_delay_ms(uint32_t delay) {
    // CIRCUITPY-CHANGE: Yield to JavaScript event loop during sleep
    // This allows JavaScript to process animations, UI updates, and async operations
    // while CircuitPython appears to be sleeping synchronously

    uint64_t start = common_hal_time_monotonic_ms();
    uint64_t target = start + delay;

    while (common_hal_time_monotonic_ms() < target) {
        // Yield control via CircuitPython's native background task system
        // This macro runs background_callback_run_all() which:
        // - Processes message queue responses from JavaScript
        // - Allows JavaScript event loop to run
        // - Handles USB, network, and other async events
        RUN_BACKGROUND_TASKS;

        // Also handle any pending Python scheduled callbacks
        mp_handle_pending(false);
    }
}
