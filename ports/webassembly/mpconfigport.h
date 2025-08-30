/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2022 Damien P. George
 * Copyright (c) 2017, 2018 Rami Ali
 * Copyright (c) 2023 CircuitPython WebAssembly Contributors
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

#ifndef MICROPY_INCLUDED_WEBASSEMBLY_MPCONFIGPORT_H
#define MICROPY_INCLUDED_WEBASSEMBLY_MPCONFIGPORT_H

// Options to control how MicroPython is built for this port, overriding
// defaults in py/mpconfig.h.

// CIRCUITPY-CHANGE: GCC version check disabled for Emscripten via Makefile
// Emscripten reports __GNUC__ = 4 but is actually based on modern Clang

// For standard integer types
#include <stdint.h>

// Platform definition
#define MICROPY_PY_SYS_PLATFORM     "webassembly"

// Variant-specific definitions.
#include "mpconfigvariant.h"

// WebAssembly specific overrides
#ifndef MICROPY_CONFIG_ROM_LEVEL
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#endif

// Enable top-level await for JavaScript async integration
#ifndef MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT (1)
#endif

// Enable garbage collection (required for finalizers)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_FINALISER (1)

// Enable floating point support
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#endif

// Disable micropython module debug features to avoid unused variable warnings
#define MICROPY_PY_MICROPYTHON_STACK_USE (0)

// Long integer implementation - use MPZ for compatibility with frozen content
#ifndef MICROPY_LONGINT_IMPL
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#endif

// Basic filesystem configuration for WebAssembly
#ifndef INTERNAL_FLASH_FILESYSTEM
#define INTERNAL_FLASH_FILESYSTEM    (0)
#endif

// Enable JavaScript integration modules
#ifndef MICROPY_PY_JS
#define MICROPY_PY_JS (MICROPY_CONFIG_ROM_LEVEL_AT_LEAST_EXTRA_FEATURES)
#endif

#ifndef MICROPY_PY_JSFFI
#define MICROPY_PY_JSFFI (MICROPY_CONFIG_ROM_LEVEL_AT_LEAST_EXTRA_FEATURES)
#endif

// Random seed function
#ifndef MICROPY_PY_RANDOM_SEED_INIT_FUNC
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (mp_js_random_u32())
uint32_t mp_js_random_u32(void);
#endif

// VM hook configuration
#ifndef MICROPY_VARIANT_ENABLE_JS_HOOK
#define MICROPY_VARIANT_ENABLE_JS_HOOK (0)
#endif

#if MICROPY_VARIANT_ENABLE_JS_HOOK
#define MICROPY_VM_HOOK_COUNT (10)
#define MICROPY_VM_HOOK_INIT static uint vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL if (--vm_hook_divisor == 0) { \
        vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
        extern void mp_js_hook(void); \
        mp_js_hook(); \
}
#ifndef MICROPY_VM_HOOK_LOOP
#define MICROPY_VM_HOOK_LOOP MICROPY_VM_HOOK_POLL
#endif
#ifndef MICROPY_VM_HOOK_RETURN
#define MICROPY_VM_HOOK_RETURN MICROPY_VM_HOOK_POLL
#endif
#endif

// Type definitions for the specific machine based on the word size.
#ifndef MICROPY_OBJ_REPR
#ifdef __LP64__
typedef long mp_int_t; // must be pointer size
typedef unsigned long mp_uint_t; // must be pointer size
#else
// These are definitions for machines where sizeof(int) == sizeof(void*),
// regardless of actual size.
typedef int mp_int_t; // must be pointer size
typedef unsigned int mp_uint_t; // must be pointer size
#endif
#else
// Assume that if we already defined the obj repr then we also defined types.
#endif

// File offset type
#if _FILE_OFFSET_BITS == 64 && !defined(__LP64__)
typedef long long mp_off_t;
#else
typedef long mp_off_t;
#endif

// We need to provide a declaration/definition of alloca()
#if !defined(MICROPY_NO_ALLOCA) || MICROPY_NO_ALLOCA == 0
#include <alloca.h>
#endif

// Machine-specific configuration
#define MP_SSIZE_MAX (0x7fffffff)
#define MP_STATE_PORT MP_STATE_VM

// CircuitPython WebAssembly specific board/MCU names
#define MICROPY_HW_BOARD_NAME "CircuitPython WebAssembly"
#define MICROPY_HW_MCU_NAME "Emscripten"

// Background tasks macro for CircuitPython compatibility
#define RUN_BACKGROUND_TASKS ((void)0)

// Event handling
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
    } while (0);

// Supervisor port functions
void port_background_tick(void);
void port_start_background_tick(void);
void port_finish_background_tick(void);
void port_wake_main_task(void);

// External printer function
extern const struct _mp_print_t mp_stderr_print;

// USB configuration (not used in WebAssembly but required by CircuitPython)
#define USB_NUM_ENDPOINT_PAIRS 8

#endif // MICROPY_INCLUDED_WEBASSEMBLY_MPCONFIGPORT_H