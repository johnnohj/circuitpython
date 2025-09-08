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

// Set base feature level suitable for WebAssembly
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

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

// Always enable GC
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_PYSTACK (1)

// Event-driven REPL for Node.js compatibility
#define MICROPY_REPL_EVENT_DRIVEN (1)

// HAL provider system
#define CIRCUITPY_HAL_PROVIDER (1)

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

// Essential modules
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT (1)

// Enable floating-point support
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_BUILTINS_FLOAT (1)
#define MICROPY_PY_MATH (1)

// HAL stdout function
extern void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len);
#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn_cooked(str, len)

// Disable unused features
#define RUN_BACKGROUND_TASKS ((void)0)

// Disable CircuitPython translations
#define CIRCUITPY_TRANSLATE (0)

// HAL pin type definition (for MicroPython compatibility)
typedef void* mp_hal_pin_obj_t;
