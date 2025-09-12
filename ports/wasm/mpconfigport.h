// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython WebAssembly Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

// Options to control how MicroPython is built for this port, overriding
// defaults in py/mpconfig.h.

// For size_t and ssize_t
#include <unistd.h>

// CircuitPython enhancement
#define CIRCUITPY_MICROPYTHON_ADVANCED (1)

// Keep BASIC_FEATURES for stability but manually enable specific functions
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_BASIC_FEATURES)

// Manually enable help function (normally requires EXTRA_FEATURES)
#define MICROPY_PY_BUILTINS_HELP (1)
#define MICROPY_PY_BUILTINS_HELP_MODULES (1)

// WebAssembly/Emscripten platform
#ifndef MICROPY_PY_SYS_PLATFORM
#define MICROPY_PY_SYS_PLATFORM "webassembly"
#endif

// Board and MCU identification
#ifndef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME "CTPY_WASM"
#endif

#ifndef MICROPY_HW_MCU_NAME
#define MICROPY_HW_MCU_NAME "Emscripten"
#endif

// Readline configuration - use VM state like MicroPython WebAssembly port  
#define MICROPY_READLINE_HISTORY_SIZE (8)
#define MP_STATE_PORT MP_STATE_VM

// Add readline history to VM state
#define MICROPY_PORT_STATE_MACHINE \
    const char *readline_hist[MICROPY_READLINE_HISTORY_SIZE];

// Always enable GC
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_PYSTACK (1)

// Event-driven REPL for Node.js compatibility
#define MICROPY_REPL_EVENT_DRIVEN (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_KBD_EXCEPTION (1)

// Remove duplicate readline configuration (already defined above)

// HAL provider system
#define CIRCUITPY_HAL_PROVIDER (1)

// Enable minimal CircuitPython modules (disabled for testing)
// #define CIRCUITPY_SUPERVISOR (1)

// Enable JavaScript FFI for hardware provider communication
#define MICROPY_PY_JSFFI (1)

// Disable filesystem support
#define MICROPY_VFS (0)

// WebAssembly memory configuration
#ifdef __EMSCRIPTEN__
#define MICROPY_ALLOC_PATH_MAX (260)
#define MP_SSIZE_MAX (0x7fffffff)
#endif

// Type definitions based on word size
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

// Cannot include <sys/types.h>, as it may lead to symbol name clashes
#if _FILE_OFFSET_BITS == 64 && !defined(__LP64__)
typedef long long mp_off_t;
#else
typedef long mp_off_t;
#endif

// GC configuration
#define MICROPY_GCREGS_SETJMP (1)
#define MICROPY_GC_ALLOC_THRESHOLD (4096)

// Stack checking
#define MICROPY_STACK_CHECK (1)

// Disable features not needed for WebAssembly
#define MICROPY_HELPER_LEXER_UNIX (0)
#define MICROPY_VFS_POSIX (0)
#define MICROPY_READER_POSIX (0)
#define MICROPY_VFS (0)
#define MICROPY_PY_BUILTINS_INPUT (0)
#define MICROPY_HELPER_READLINE (0)

// Essential modules
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT (1)

// Basic Python built-ins needed for object operations
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_PY_BUILTINS_STR_COUNT (1)
#define MICROPY_PY_BUILTINS_STR_OP_MODULO (1)
#define MICROPY_PY_BUILTINS_BYTES (1)
#define MICROPY_PY_BUILTINS_BYTEARRAY (1)
#define MICROPY_PY_BUILTINS_SET (1)
#define MICROPY_PY_BUILTINS_FROZENSET (1)
#define MICROPY_PY_BUILTINS_PROPERTY (1)
#define MICROPY_PY_BUILTINS_RANGE_ATTRS (1)
#define MICROPY_PY_BUILTINS_ENUMERATE (1)
#define MICROPY_PY_BUILTINS_FILTER (1)
#define MICROPY_PY_BUILTINS_MAP (1)
#define MICROPY_PY_BUILTINS_ZIP (1)
#define MICROPY_PY_BUILTINS_REVERSED (1)

// Enable floating-point support
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_FLOAT (1)
#define MICROPY_PY_MATH (1)

// HAL stdout function - provided by shared/runtime/stdout_helpers.c
extern void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);
#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn_cooked(str, len)

// Disable unused features
#define RUN_BACKGROUND_TASKS ((void)0)

// Disable CircuitPython translations
#define CIRCUITPY_TRANSLATE (0)

// HAL pin type definition (for MicroPython compatibility)
typedef void* mp_hal_pin_obj_t;
