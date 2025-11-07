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

// CIRCUITPY-CHANGE
#pragma once

// Options to control how MicroPython is built for this port, overriding
// defaults in py/mpconfig.h.

#include <stdint.h>
#include <stdlib.h> // for malloc, for MICROPY_GC_SPLIT_HEAP_AUTO
#include <errno.h>  // for errno used throughout the codebase
#include <stdbool.h>

// CIRCUITPY-CHANGE: Include supervisor headers for RUN_BACKGROUND_TASKS
#include "supervisor/background_callback.h"

// Variant-specific definitions.
#include "mpconfigvariant.h"

// ============================================================================
// PORT-WIDE MICROPYTHON CONFIGURATION
// These settings apply to all WASM variants and define the MicroPython
// feature set available on the WebAssembly platform.
// ============================================================================

// --- Compiler optimizations (port-wide) ---
#define MICROPY_COMP_CONST_FOLDING  (1)
#define MICROPY_COMP_CONST_LITERAL  (1)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN (1)
#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (1)
#define MICROPY_OPT_MPZ_BITWISE     (1)

// --- Reader configuration (port-wide) ---
#define MICROPY_READER_POSIX        (MICROPY_VFS)
#define MICROPY_READER_VFS          (MICROPY_VFS)

// --- Built-in help system (port-wide) ---
#define MICROPY_PY_BUILTINS_HELP    (1)
#define MICROPY_PY_BUILTINS_HELP_TEXT circuitpython_help_text
#define MICROPY_PY_BUILTINS_HELP_MODULES (1)

// --- I/O and string features (port-wide) ---
#define MICROPY_PY_IO_BUFFEREDWRITER (1)
#define MICROPY_PY_FSTRINGS         (1)

// --- System module features (port-wide) ---
#define MICROPY_PY_SYS_STDFILES     (1)
#define MICROPY_PY_SYS_STDIO_BUFFER (1)
#define MICROPY_PY_SYS_ATEXIT       (1)
#define MICROPY_PY_SYS_EXC_INFO     (1)

// --- Debugging and error handling (port-wide) ---
#define MICROPY_DEBUG_PRINTERS      (1)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)

// --- Memory and GC settings (port-wide) ---
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_PY_GC_COLLECT_RETVAL   (1)

// --- Code loading (port-wide) ---
// Defined again below for clarity, but this is the port-wide default
// #define MICROPY_PERSISTENT_CODE_LOAD (1)

// --- Float implementation (port-wide) ---
// WASM always uses double-precision floats (not single-precision)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_DOUBLE)

// --- Python compatibility (port-wide) ---
#define MICROPY_CPYTHON_COMPAT      (1)

// --- Keyboard interrupt (port-wide) ---
#define MICROPY_ASYNC_KBD_INTR      (1)

// --- REPL conveniences (port-wide) ---
#define MICROPY_REPL_INFO           (1)
#define MICROPY_REPL_EMACS_WORDS_MOVE  (1)
#define MICROPY_REPL_EMACS_EXTRA_WORDS_MOVE (1)
#define MICROPY_USE_READLINE_HISTORY   (1)
#ifndef MICROPY_READLINE_HISTORY_SIZE
#define MICROPY_READLINE_HISTORY_SIZE  (100)
#endif

// ============================================================================
// CIRCUITPY-SPECIFIC SETTINGS
// ============================================================================

// CIRCUITPY-CHANGE: CircuitPython-specific settings
#define CIRCUITPY_MICROPYTHON_ADVANCED (1)
// MICROPY_PY_ASYNC_AWAIT and CIRCUITPY_STATUS_BAR are set in mpconfigport.mk
#define MICROPY_PY_UCTYPES (0)
#define MICROPY_PY_MICROPYTHON_RINGIO (0)  // Not supported in WASM

// Board ID for CircuitPython
#ifndef CIRCUITPY_BOARD_ID
#define CIRCUITPY_BOARD_ID "webassembly"
#endif

// Banner machine name for REPL
#ifndef MICROPY_BANNER_MACHINE
#define MICROPY_BANNER_MACHINE "WebAssembly with Emscripten"
#endif

#ifndef MICROPY_CONFIG_ROM_LEVEL
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#endif

#define MICROPY_ALLOC_PATH_MAX      (256)
#define MICROPY_PERSISTENT_CODE_LOAD (1)
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT (1)
// MICROPY_READER_VFS is defined in port-wide section above
#define MICROPY_ENABLE_GC           (1)
// CIRCUITPY-CHANGE: Enable selective GC collection
#define MICROPY_ENABLE_SELECTIVE_COLLECT (1)
#define MICROPY_ENABLE_PYSTACK      (1)
#define MICROPY_KBD_EXCEPTION       (1)
#define MICROPY_REPL_EVENT_DRIVEN   (1)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_ENABLE_DOC_STRING   (1)
#define MICROPY_WARNINGS            (1)
// CIRCUITPY-CHANGE: Use mp_plat_print (stdout) for Python errors, keep stderr for JS/TS infrastructure
#define MICROPY_ERROR_PRINTER       (&mp_plat_print)
// MICROPY_FLOAT_IMPL is defined in port-wide section above (line 88)
#define MICROPY_USE_INTERNAL_ERRNO  (1)
#define MICROPY_USE_INTERNAL_PRINTF (0)
#define MICROPY_PY_BOUND_METHOD_FULL_EQUALITY_CHECK (1)

// CIRCUITPY-CHANGE: Enable finalizers
#define MICROPY_ENABLE_FINALISER    (1)

#define MICROPY_EPOCH_IS_1970       (1)
#define MICROPY_PY_ASYNCIO_TASK_QUEUE_PUSH_CALLBACK (1)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (mp_js_random_u32())
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (1)
#define MICROPY_PY_TIME_TIME_TIME_NS (1)
#define MICROPY_PY_TIME_INCLUDEFILE "ports/wasm/modtime.c"
#ifndef MICROPY_VFS
#define MICROPY_VFS                 (1)
#endif
#define MICROPY_VFS_POSIX           (MICROPY_VFS)
#define MICROPY_PY_SYS_PLATFORM     "webassembly"

// CIRCUITPY-CHANGE: FAT filesystem settings
#define MICROPY_FATFS_ENABLE_LFN       (1)
#define MICROPY_FATFS_RPATH            (2)
#define MICROPY_FATFS_MAX_SS           (4096)
#define MICROPY_FATFS_LFN_CODE_PAGE    437
#define MICROPY_FATFS_MKFS_FAT32       (1)
#define MICROPY_FATFS_USE_LABEL        (1)

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
#define MICROPY_VM_HOOK_LOOP MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN MICROPY_VM_HOOK_POLL
#endif

// CIRCUITPY-CHANGE: Background tasks hook
#define RUN_BACKGROUND_TASKS (background_callback_run_all())

// type definitions for the specific machine

#define MP_SSIZE_MAX (0x7fffffff)

// This port is intended to be 32-bit, but unfortunately, int32_t for
// different targets may be defined in different ways - either as int
// or as long. This requires different printf formatting specifiers
// to print such value. So, we avoid int32_t and use int directly.
typedef int mp_int_t; // must be pointer size
typedef unsigned mp_uint_t; // must be pointer size
typedef long mp_off_t;

// CIRCUITPY-CHANGE: Single processor for WASM
#define CIRCUITPY_PROCESSOR_COUNT (1)

#define MP_STATE_PORT MP_STATE_VM

#if MICROPY_VFS
// _GNU_SOURCE must be defined to get definitions of DT_xxx symbols from dirent.h.
#define _GNU_SOURCE
#endif

// CIRCUITPY-CHANGE: Separate stdout (Python) from stderr (JS/TS infrastructure)
extern const struct _mp_print_t mp_plat_print;   // stdout - for all Python output
extern const struct _mp_print_t mp_stderr_print; // stderr - for JS/TS messages

uint32_t mp_js_random_u32(void);