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

// CIRCUITPY-CHANGE: Use CircuitPython's default VM hook (RUN_BACKGROUND_TASKS)
// We implement our cooperative yielding in port_background_task() instead
// This ensures proper integration with the supervisor/background callback system

// Don't override MICROPY_VARIANT_ENABLE_JS_HOOK - let CircuitPython's defaults work
// Don't override VM hooks - let CircuitPython's py/circuitpy_mpconfig.h define them
// The default is: MICROPY_VM_HOOK_LOOP = RUN_BACKGROUND_TASKS
// Which calls background_callback_run_all() which calls our port_background_task()

