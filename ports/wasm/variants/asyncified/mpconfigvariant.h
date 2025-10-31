/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
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

// ============================================================================
// ASYNCIFIED VARIANT CONFIGURATION
// ============================================================================
//
// This variant uses Emscripten's ASYNCIFY to enable true cooperative yielding.
//
// Key features:
// - Emscripten ASYNCIFY enabled (see mpconfigvariant.mk)
// - VM hook enabled to periodically yield to JavaScript
// - Preserves ALL execution state (loop iterators, generators, etc.)
// - Works exactly like hardware CircuitPython supervisor
//
// How it works:
// - MICROPY_VM_HOOK_LOOP calls mp_js_hook() every 10 bytecodes
// - mp_js_hook() can call emscripten_sleep(0) to yield
// - ASYNCIFY unwinds/rewinds C stack, preserving state
// - Browser remains responsive during long-running loops
//
// Tradeoffs:
// - Larger binary size (~50-100KB increase)
// - Slight performance overhead
// - But: Robust, correct, handles ALL Python code patterns
//
// For simpler exception-based yielding, use "integrated" variant.
// For no yielding (lightest build), use "standard" variant.
// ============================================================================

// Set base feature level - full featured but optimized for WASM
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// WASM has no physical status LEDs, disable status bar features
#define CIRCUITPY_STATUS_BAR 0

// IMPORTANT: Do NOT use MICROPY_VARIANT_ENABLE_JS_HOOK for asyncified variant
// It calls a JS library function which creates problematic boundaries for ASYNCIFY
// Instead, we define our own VM hook that calls pure C code directly
#define MICROPY_VARIANT_ENABLE_JS_HOOK (0)

// EMSCRIPTEN_ASYNCIFY_ENABLED is defined in mpconfigvariant.mk as a CFLAG
// This tells supervisor/port.c to compile the C implementation

// Override VM hook to call pure C function (not JS library function)
// This avoids JS/C boundary issues that confuse ASYNCIFY
#define MICROPY_VM_HOOK_COUNT (10)
#define MICROPY_VM_HOOK_INIT static uint vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL if (--vm_hook_divisor == 0) { \
        vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
        extern void mp_js_hook_asyncify_impl(void); \
        mp_js_hook_asyncify_impl(); \
}
#define MICROPY_VM_HOOK_LOOP MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN MICROPY_VM_HOOK_POLL

