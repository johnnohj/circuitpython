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

// CIRCUITPY-CHANGE: Stub implementations for CircuitPython supervisor functions
// These are needed by CircuitPython but not used in WASM environment

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "py/mpconfig.h"
#include "py/misc.h"
#include "supervisor/shared/translate/translate.h"
#include <emscripten.h>

// Forward declare types we need
typedef struct _fs_user_mount_t fs_user_mount_t;

// Safe mode reset stub - not applicable for WASM
void reset_into_safe_mode(int reason) {
    (void)reason;
    // In WASM, we can't reset - just ignore
}

// Message decompression and serial write are provided by
// supervisor/shared/translate/translate.c, which we'll include in the build

// Stack checking stub
bool stack_ok(void) {
    return true; // Assume stack is always OK in WASM
}

// Heap checking stub
void assert_heap_ok(void) {
    // No-op in WASM
}

// Filesystem writable stubs - not applicable for WASM
bool filesystem_is_writable_by_python(fs_user_mount_t *vfs) {
    (void)vfs;
    return true; // Always writable in WASM
}

void filesystem_set_writable_by_usb(fs_user_mount_t *vfs, bool writable) {
    (void)vfs;
    (void)writable;
    // No-op in WASM
}

// FAT filesystem timestamp function
// Returns a packed date/time value in FAT format
// Bit fields: Y(7) M(4) D(5) H(5) M(6) S(5)
// Uses emscripten_get_now() to get current time
uint32_t get_fattime(void) {
    // Get current time in milliseconds since epoch
    double now_ms = emscripten_get_now();
    time_t now_sec = (time_t)(now_ms / 1000.0);

    // Convert to broken-down time (UTC)
    struct tm *timeinfo = gmtime(&now_sec);

    if (timeinfo == NULL) {
        // Fallback to a fixed date if time conversion fails
        // 2024-01-01 00:00:00
        return ((uint32_t)(44 << 25) | (1 << 21) | (1 << 16));
    }

    // FAT timestamp format:
    // Year: 7 bits (0-127, representing 1980-2107)
    // Month: 4 bits (1-12)
    // Day: 5 bits (1-31)
    // Hour: 5 bits (0-23)
    // Minute: 6 bits (0-59)
    // Second: 5 bits (0-29, representing 0-58 in 2-second intervals)

    uint32_t year = (timeinfo->tm_year + 1900) - 1980; // tm_year is years since 1900
    uint32_t month = timeinfo->tm_mon + 1;             // tm_mon is 0-11
    uint32_t day = timeinfo->tm_mday;                  // 1-31
    uint32_t hour = timeinfo->tm_hour;                 // 0-23
    uint32_t minute = timeinfo->tm_min;                // 0-59
    uint32_t second = timeinfo->tm_sec / 2;            // 0-29 (2-second resolution)

    return ((year & 0x7F) << 25) |
           ((month & 0x0F) << 21) |
           ((day & 0x1F) << 16) |
           ((hour & 0x1F) << 11) |
           ((minute & 0x3F) << 5) |
           (second & 0x1F);
}
