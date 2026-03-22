/*
 * WASI port configuration
 *
 * Headless CircuitPython VM compiled with wasi-sdk.
 * Based on the unix port pattern: sets CIRCUITPY=1 in CFLAGS for
 * CircuitPython code paths in py/*.c, but does NOT include
 * py/circuitpy_mpconfig.h (no full supervisor infrastructure yet).
 *
 * VM hooks defined directly by the port (same as unix).
 */
#pragma once

#include <unistd.h>

// Variant-specific definitions
#include "mpconfigvariant.h"

// ITCM/DTCM linker macros (no-ops for WASI)
#include "linker.h"

#define CIRCUITPY_MICROPYTHON_ADVANCED (1)
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_PY_UCTYPES (0)

#ifndef MICROPY_CONFIG_ROM_LEVEL
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)
#endif

#define MICROPY_PY_SYS_PLATFORM "wasi"
#define MICROPY_PY_SYS_PATH_DEFAULT ".frozen:/lib"

#define MP_STATE_PORT MP_STATE_VM

// ---- Type definitions (wasm32: sizeof(void*) == sizeof(int) == 4) ----
typedef int mp_int_t;
typedef unsigned int mp_uint_t;
// mp_off_t: the OPFS variant includes circuitpy_mpconfig.h which
// typedefs it as `long`. For the standard variant, define here.
#ifndef MICROPY_OPFS_EXECUTOR
typedef long long mp_off_t;
#endif

// alloca
#include <alloca.h>

// ---- GC ----
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_GCREGS_SETJMP       (1)
#define MICROPY_ENABLE_SELECTIVE_COLLECT (1)

// ---- VFS (POSIX — wasi-libc maps to WASI fd_* syscalls) ----
#define MICROPY_ENABLE_FINALISER    (1)
#define MICROPY_VFS                 (1)
#define MICROPY_READER_VFS          (1)
#define MICROPY_VFS_POSIX           (1)
#define MICROPY_READER_POSIX        (1)
#define MICROPY_EPOCH_IS_1970       (1)

// ---- Stackless ----
#ifndef MICROPY_STACKLESS
#define MICROPY_STACKLESS           (0)
#define MICROPY_STACKLESS_STRICT    (0)
#endif

// ---- Path limits ----
#ifndef PATH_MAX
#define PATH_MAX 256
#endif
#define MICROPY_ALLOC_PATH_MAX      (PATH_MAX)

// Import and sys.path
#define MICROPY_MODULE_OVERRIDE_MAIN_IMPORT (1)
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS (1)

// ---- Debugging / error output ----
extern const struct _mp_print_t mp_stderr_print;
#define MICROPY_DEBUG_PRINTER (&mp_stderr_print)
#define MICROPY_ERROR_PRINTER (&mp_stderr_print)

// ---- Platform identification ----
#define MICROPY_HW_BOARD_NAME   "WASM-WASI"
#define MICROPY_HW_MCU_NAME     "wasm32"

// ---- CircuitPython feature flags needed by shared-bindings/ headers ----
// These are normally set by mpconfigboard.h (included via circuitpy_mpconfig.h),
// but we don't include that. Define them here for headers that check them.
#ifndef CIRCUITPY_PROCESSOR_COUNT
#define CIRCUITPY_PROCESSOR_COUNT   (1)
#endif
#ifndef CIRCUITPY_NVM
#define CIRCUITPY_NVM               (0)
#endif
#ifndef CIRCUITPY_WATCHDOG
#define CIRCUITPY_WATCHDOG          (0)
#endif

// ---- VM hooks + background tasks ----
// OPFS variant: uses py/circuitpy_mpconfig.h which provides
//   MICROPY_VM_HOOK_LOOP → RUN_BACKGROUND_TASKS → background_callback_run_all()
//   background_callback_run_all() calls port_background_task() (supervisor/port.c)
//   then runs queued callbacks including supervisor_background_tick
// Standard variant: no-op hooks.
#ifndef MICROPY_OPFS_EXECUTOR
#define RUN_BACKGROUND_TASKS        ((void)0)
#endif

// ---- dirent ----
#ifndef _DIRENT_HAVE_D_TYPE
#define _DIRENT_HAVE_D_TYPE (1)
#endif
#ifndef _DIRENT_HAVE_D_INO
#define _DIRENT_HAVE_D_INO (1)
#endif

#include <stdio.h>

// ---- CircuitPython base config for OPFS variant ----
// Provides RUN_BACKGROUND_TASKS, VM hooks, type sizes, etc.
// Must come AFTER our type definitions. mpconfigboard.h is included
// by circuitpy_mpconfig.h — provides board-level feature flags.
#ifdef MICROPY_OPFS_EXECUTOR
#include "py/circuitpy_mpconfig.h"
#endif
