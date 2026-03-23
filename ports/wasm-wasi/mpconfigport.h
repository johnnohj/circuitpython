/*
 * WASI port configuration
 *
 * CircuitPython VM compiled with wasi-sdk. Both variants (standard
 * and OPFS) include py/circuitpy_mpconfig.h for full CircuitPython
 * configuration. Board-level CIRCUITPY_* flags are in mpconfigboard.h.
 *
 * This file provides:
 *   - WASM/WASI platform adaptations (types, VFS, paths)
 *   - Definitions that must precede circuitpy_mpconfig.h
 *   - circuitpy_mpconfig.h include (which pulls in mpconfigboard.h)
 */
#pragma once

#include <unistd.h>

// Variant-specific definitions (ROM level, emitter disables, etc.)
#include "mpconfigvariant.h"

// ITCM/DTCM linker macros (no-ops for WASI)
#include "linker.h"

#define CIRCUITPY_MICROPYTHON_ADVANCED (1)
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
// mp_off_t provided by circuitpy_mpconfig.h (typedef long).

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

// ---- VM hooks + background tasks ----
// circuitpy_mpconfig.h provides RUN_BACKGROUND_TASKS →
// background_callback_run_all() → port_background_task()

// ---- VM yield (reactor variant) ----
// When enabled, the VM checks mp_vm_should_yield() at branch points
// and returns MP_VM_RETURN_YIELD with state saved for resumption.
#if MICROPY_VM_YIELD_ENABLED
extern int mp_vm_should_yield(void);
extern void *mp_vm_yield_state;
#define MICROPY_VM_YIELD_SAVE_STATE(cs) do { mp_vm_yield_state = (void *)(cs); } while (0)
#endif

// ---- dirent ----
#ifndef _DIRENT_HAVE_D_TYPE
#define _DIRENT_HAVE_D_TYPE (1)
#endif
#ifndef _DIRENT_HAVE_D_INO
#define _DIRENT_HAVE_D_INO (1)
#endif

#include <stdio.h>

// ---- CircuitPython base config ----
// Provides module enables, VM hooks, type sizes, etc.
// Must come AFTER our type definitions. mpconfigboard.h is included
// by circuitpy_mpconfig.h — provides board-level CIRCUITPY_* flags.
#include "py/circuitpy_mpconfig.h"

// ---- Force REPL output through mp_hal_stdout_tx_strn ----
// circuitpy_mpconfig.h sets MICROPY_PY_SYS_STDFILES=1, which makes
// MP_PYTHON_PRINTER (in mpprint.h) use sys.stdout (a VFS posix fd).
// That bypasses mp_hal_stdout_tx_strn and our terminal display hook.
// Override the condition so mpprint.h picks the mp_plat_print path.
#undef MICROPY_PY_SYS_STDFILES
#define MICROPY_PY_SYS_STDFILES (0)

// circuitpy_mpconfig.h unconditionally sets MICROPY_REPL_EVENT_DRIVEN=0.
// Override it here if the variant header set it to 1.
#ifdef MICROPY_WORKER
#undef MICROPY_REPL_EVENT_DRIVEN
#define MICROPY_REPL_EVENT_DRIVEN (1)
#endif
