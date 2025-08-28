/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2022 Damien P. George
 * Copyright (c) 2017, 2018 Rami Ali
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

// Options to control how MicroPython is built for this port, overriding
// defaults in py/mpconfig.h.

// CIRCUITPY-CHANGE: Disable GCC version check for Emscripten
// Emscripten reports __GNUC__ = 4 but is actually based on modern Clang
#ifdef __EMSCRIPTEN__
#define CIRCUITPY_MIN_GCC_VERSION 0
#endif

// CIRCUITPY-CHANGE: Include CircuitPython-specific config first
#include "py/circuitpy_mpconfig.h"

// CIRCUITPY-CHANGE: Set qstr length configuration for WebAssembly (after circuitpy_mpconfig.h)
#ifndef MICROPY_QSTR_BYTES_IN_LEN
#define MICROPY_QSTR_BYTES_IN_LEN (1)
#endif

#include <stdint.h>
#include <stdlib.h> // for malloc, for MICROPY_GC_SPLIT_HEAP_AUTO

// Variant-specific definitions.
#include "mpconfigvariant.h"

#ifndef MICROPY_CONFIG_ROM_LEVEL
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#endif

#ifndef MICROPY_ALLOC_PATH_MAX
#define MICROPY_ALLOC_PATH_MAX      (256)
#endif
#ifndef MICROPY_PERSISTENT_CODE_LOAD
#define MICROPY_PERSISTENT_CODE_LOAD (1)
#endif
#ifndef MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT (1)
#endif
#ifndef MICROPY_READER_VFS
#define MICROPY_READER_VFS          (MICROPY_VFS)
#endif
#ifndef MICROPY_ENABLE_GC
#define MICROPY_ENABLE_GC           (1)
#endif
#ifndef MICROPY_ENABLE_PYSTACK
#define MICROPY_ENABLE_PYSTACK      (1)
#endif
#ifndef MICROPY_KBD_EXCEPTION
#define MICROPY_KBD_EXCEPTION       (1)
#endif
#ifndef MICROPY_REPL_EVENT_DRIVEN
#define MICROPY_REPL_EVENT_DRIVEN   (1)
#endif
#ifndef MICROPY_LONGINT_IMPL
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_MPZ)
#endif
#ifndef MICROPY_ENABLE_DOC_STRING
#define MICROPY_ENABLE_DOC_STRING   (1)
#endif
#ifndef MICROPY_WARNINGS
#define MICROPY_WARNINGS            (1)
#endif
#ifndef MICROPY_ERROR_PRINTER
#define MICROPY_ERROR_PRINTER       (&mp_stderr_print)
#endif
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_DOUBLE)
#endif
#ifndef MICROPY_USE_INTERNAL_ERRNO
#define MICROPY_USE_INTERNAL_ERRNO  (1)
#endif
#ifndef MICROPY_USE_INTERNAL_PRINTF
#define MICROPY_USE_INTERNAL_PRINTF (0)
#endif
#ifndef MICROPY_PY_BOUND_METHOD_FULL_EQUALITY_CHECK
#define MICROPY_PY_BOUND_METHOD_FULL_EQUALITY_CHECK (1)
#endif

#ifndef MICROPY_EPOCH_IS_1970
#define MICROPY_EPOCH_IS_1970       (1)
#endif
#ifndef MICROPY_PY_ASYNCIO_TASK_QUEUE_PUSH_CALLBACK
#define MICROPY_PY_ASYNCIO_TASK_QUEUE_PUSH_CALLBACK (1)
#endif
#ifndef MICROPY_PY_RANDOM_SEED_INIT_FUNC
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (mp_js_random_u32())
#endif
#ifndef MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#endif
#ifndef MICROPY_PY_TIME_TIME_TIME_NS
#define MICROPY_PY_TIME_TIME_TIME_NS (1)
#endif
#ifndef MICROPY_PY_TIME_INCLUDEFILE
#define MICROPY_PY_TIME_INCLUDEFILE "ports/webassembly/modtime.c"
#endif
#ifndef MICROPY_VFS
#define MICROPY_VFS                 (1)
#endif
#ifndef MICROPY_VFS_POSIX
#define MICROPY_VFS_POSIX           (MICROPY_VFS)
#endif
// CIRCUITPY-CHANGE: Set CircuitPython platform
#define MICROPY_PY_SYS_PLATFORM     "webassembly"
#ifndef CIRCUITPY_WEBASSEMBLY
#define CIRCUITPY_WEBASSEMBLY        (1)
#endif

// CIRCUITPY-CHANGE: Set filesystem configuration for WebAssembly
#ifndef INTERNAL_FLASH_FILESYSTEM
#define INTERNAL_FLASH_FILESYSTEM    (0)
#endif
#ifndef QSPI_FLASH_FILESYSTEM
#define QSPI_FLASH_FILESYSTEM        (0)
#endif
#ifndef SPI_FLASH_FILESYSTEM
#define SPI_FLASH_FILESYSTEM         (0)
#endif

#ifndef MICROPY_PY_JS
#define MICROPY_PY_JS (MICROPY_CONFIG_ROM_LEVEL_AT_LEAST_EXTRA_FEATURES)
#endif

#ifndef MICROPY_PY_JSFFI
#define MICROPY_PY_JSFFI (MICROPY_CONFIG_ROM_LEVEL_AT_LEAST_EXTRA_FEATURES)
#endif

#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
    } while (0);

// Whether the VM will periodically call mp_js_hook(), which checks for
// interrupt characters on stdin (or equivalent input).
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

// type definitions for the specific machine

#define MP_SSIZE_MAX (0x7fffffff)

// This port is intended to be 32-bit, but unfortunately, int32_t for
// different targets may be defined in different ways - either as int
// or as long. This requires different printf formatting specifiers
// to print such value. So, we avoid int32_t and use int directly.
// (Types already defined in CircuitPython's circuitpy_mpconfig.h)

// CIRCUITPY-CHANGE: Set CircuitPython board/MCU names
#define MICROPY_HW_BOARD_NAME "CircuitPython WebAssembly"
#define MICROPY_HW_MCU_NAME "Emscripten"

#define MP_STATE_PORT MP_STATE_VM

#if MICROPY_VFS
// _GNU_SOURCE must be defined to get definitions of DT_xxx symbols from dirent.h.
#define _GNU_SOURCE
#endif

// CIRCUITPY-CHANGE: Define translation macros for WebAssembly
#ifndef NO_QSTR
#include "supervisor/shared/translate/translate.h"
#endif

// CIRCUITPY-CHANGE: Safe mode and reset functionality
#include "supervisor/shared/safe_mode.h"

// WebAssembly-specific safe mode reasons (empty for now)
// These can be expanded later for WebAssembly-specific error conditions

extern const struct _mp_print_t mp_stderr_print;

uint32_t mp_js_random_u32(void);

// CIRCUITPY-CHANGE: Enable CircuitPython supervisor features
#include "supervisor/spi_flash_api.h"

// CIRCUITPY-CHANGE: CircuitPython-specific feature flags
#ifndef CIRCUITPY_INTERNAL_NVM_SIZE
#define CIRCUITPY_INTERNAL_NVM_SIZE         (0)
#endif

#ifndef CIRCUITPY_BOOT_BUTTON
#define CIRCUITPY_BOOT_BUTTON               (0)
#endif

// Basic modules for WebAssembly
#define CIRCUITPY_BOARD                     (1)
#define CIRCUITPY_DIGITALIO                 (0)  // Disabled for WebAssembly
#define CIRCUITPY_ANALOGIO                  (0)  // Disabled for WebAssembly
#define CIRCUITPY_MICROCONTROLLER           (1)
#define CIRCUITPY_OS                        (1)
#define CIRCUITPY_SUPERVISOR                (1)
#define CIRCUITPY_STORAGE                   (0)  // Disabled for now
#define CIRCUITPY_USB_CDC                   (0)  // Disabled for WebAssembly
#define CIRCUITPY_USB_HID                   (0)  // Disabled for WebAssembly
#define CIRCUITPY_USB_MIDI                  (0)  // Disabled for WebAssembly
#define CIRCUITPY_USB_MSC                   (0)  // Disabled for WebAssembly

// Filesystem is disabled via Makefile command line flag

// Enable basic I/O and system modules
#define CIRCUITPY_SYS                       (1)
#define CIRCUITPY_TIME                      (1)
#define CIRCUITPY_MATH                      (1)
#define CIRCUITPY_RANDOM                    (1)

// Define required supervisor port functions
void port_background_tick(void);
void port_start_background_tick(void);
void port_finish_background_tick(void);
void port_wake_main_task(void);
