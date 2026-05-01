// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2013, 2014 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/mpconfigport.h (unix port derivative)
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// Design refs:
//   design/wasm-layer.md  (Option A: adopt upstream supervisor)

#pragma once

// Options to control how MicroPython is built for this port, overriding
// defaults in py/mpconfig.h. This file is mostly about configuring the
// features to work on Unix-like systems, see mpconfigvariant.h (and
// mpconfigvariant_common.h) for feature enabling.

// For size_t and ssize_t
#include <unistd.h>

// Variant-specific definitions.
#include "mpconfigvariant.h"

// ── WASI overrides ──
// The variant header (via mpconfigvariant_common.h) enables features
// that don't work on WASI. Override them here after the include.
#ifdef __wasi__
// No POSIX signals — interrupt via MEMFS rx buffer
#undef MICROPY_ASYNC_KBD_INTR
#define MICROPY_ASYNC_KBD_INTR      (0)

// WASI poll constants don't match MP_STREAM_POLL_* values
#undef MICROPY_PY_SELECT_POSIX_OPTIMISATIONS
#define MICROPY_PY_SELECT_POSIX_OPTIMISATIONS (0)

// No os.system() on WASI
#undef MICROPY_PY_OS_SYSTEM
#define MICROPY_PY_OS_SYSTEM        (0)

// RingIO — pollable ring buffer stream, used for MEMFS communication endpoints
#undef MICROPY_PY_MICROPYTHON_RINGIO
#define MICROPY_PY_MICROPYTHON_RINGIO (1)

// Force print() through mp_hal_stdout_tx_strn (HAL) instead of VFS fd.
// This ensures all output flows through our routing in supervisor/micropython.c.
#undef MICROPY_PY_SYS_STDFILES
#define MICROPY_PY_SYS_STDFILES     (0)

// No /dev/mem on WASI
#undef MICROPY_PLAT_DEV_MEM
#define MICROPY_PLAT_DEV_MEM        (0)

// Platform string
#undef MICROPY_PY_SYS_PLATFORM
#define MICROPY_PY_SYS_PLATFORM     "wasi"
#endif

// Console UART printf — upstream's diagnostic channel.
// On real boards this goes to a debug UART.  For us, route to stderr
// which maps to console.error in the browser (visible in DevTools,
// not in the REPL terminal).  Could be routed to a secondary UI view
// for data plots or diagnostics in the future.
int console_uart_printf(const char *fmt, ...);
#define CIRCUITPY_CONSOLE_UART_PRINTF(...) console_uart_printf(__VA_ARGS__)

// Platform compiler string for banner
#ifndef MICROPY_PLATFORM_COMPILER
#if defined(__clang__)
#define MICROPY_PLATFORM_COMPILER \
    "Clang " \
    MP_STRINGIFY(__clang_major__) "." \
    MP_STRINGIFY(__clang_minor__) "." \
    MP_STRINGIFY(__clang_patchlevel__)
#else
#define MICROPY_PLATFORM_COMPILER ""
#endif
#endif

#define CIRCUITPY_MICROPYTHON_ADVANCED (1)
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_PY_UCTYPES (0)

#ifndef MICROPY_CONFIG_ROM_LEVEL
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)
#endif

#ifndef MICROPY_PY_SYS_PLATFORM
#if defined(__APPLE__) && defined(__MACH__)
    #define MICROPY_PY_SYS_PLATFORM  "darwin"
#else
    #define MICROPY_PY_SYS_PLATFORM  "linux"
#endif
#endif

#ifndef MICROPY_PY_SYS_PATH_DEFAULT
#ifdef __wasi__
#define MICROPY_PY_SYS_PATH_DEFAULT ".frozen"
#else
#define MICROPY_PY_SYS_PATH_DEFAULT ".frozen:~/.micropython/lib:/usr/lib/micropython"
#endif
#endif

#define MP_STATE_PORT MP_STATE_VM

// Configure which emitter to use for this target.
// WASM has no native emitters — bytecode interpreter IS wasm.
#if !defined(__wasm__)
#if !defined(MICROPY_EMIT_X64) && defined(__x86_64__)
    #define MICROPY_EMIT_X64        (1)
#endif
#if !defined(MICROPY_EMIT_X86) && defined(__i386__)
    #define MICROPY_EMIT_X86        (1)
#endif
#if !defined(MICROPY_EMIT_THUMB) && defined(__thumb2__)
    #define MICROPY_EMIT_THUMB      (1)
    #define MICROPY_MAKE_POINTER_CALLABLE(p) ((void *)((mp_uint_t)(p) | 1))
#endif
#if !defined(MICROPY_EMIT_ARM) && defined(__arm__) && !defined(__thumb2__)
    #define MICROPY_EMIT_ARM        (1)
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

// Cannot include <sys/types.h>, as it may lead to symbol name clashes
#if _FILE_OFFSET_BITS == 64 && !defined(__LP64__)
typedef long long mp_off_t;
#else
typedef long mp_off_t;
#endif

// We need to provide a declaration/definition of alloca()
#if !defined(MICROPY_NO_ALLOCA) || MICROPY_NO_ALLOCA == 0
#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#else
#include <alloca.h>
#endif
#endif

// Always enable GC.
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_ENABLE_SELECTIVE_COLLECT (1)

#if !(defined(MICROPY_GCREGS_SETJMP) || defined(__x86_64__) || defined(__i386__) || defined(__thumb2__) || defined(__thumb__) || defined(__arm__) || (defined(__riscv) && (__riscv_xlen == 64)))
#define MICROPY_GCREGS_SETJMP (1)
#endif

// Enable the VFS, and enable the posix "filesystem".
#define MICROPY_ENABLE_FINALISER    (1)
#define MICROPY_VFS                 (1)
#define MICROPY_READER_VFS          (1)
#define MICROPY_HELPER_LEXER_UNIX   (1)
#define MICROPY_VFS_POSIX           (1)
#define MICROPY_READER_POSIX        (1)
#ifndef MICROPY_TRACKED_ALLOC
#define MICROPY_TRACKED_ALLOC       (MICROPY_PY_FFI || MICROPY_BLUETOOTH_BTSTACK)
#endif

#define MICROPY_EPOCH_IS_1970       (1)
#define MICROPY_SELECT_REMAINING_TIME (1)

#ifndef MICROPY_STACKLESS
#define MICROPY_STACKLESS           (0)
#define MICROPY_STACKLESS_STRICT    (0)
#endif

// Implementation of the machine module.
#ifdef __wasi__
#define MICROPY_PY_MACHINE_INCLUDEFILE "ports/wasm-tmp/modmachine.c"
#define MICROPY_MACHINE_MEM_GET_READ_ADDR   mod_machine_mem_get_addr
#define MICROPY_MACHINE_MEM_GET_WRITE_ADDR  mod_machine_mem_get_addr
#endif

#define MICROPY_FATFS_ENABLE_LFN       (1)
#define MICROPY_FATFS_RPATH            (2)
#define MICROPY_FATFS_MAX_SS           (4096)
#define MICROPY_FATFS_LFN_CODE_PAGE    437
#define MICROPY_FATFS_MKFS_FAT32       (1)
#define MICROPY_FATFS_USE_LABEL (1)

#define MICROPY_ALLOC_PATH_MAX      (PATH_MAX)
#define MICROPY_MODULE_OVERRIDE_MAIN_IMPORT (1)
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS (0)
#define MICROPY_PY_SYS_EXECUTABLE (1)

extern const struct _mp_print_t mp_stderr_print;
#define MICROPY_DEBUG_PRINTER (&mp_stderr_print)
#define MICROPY_ERROR_PRINTER (&mp_stderr_print)

// Random seed init
#ifdef MICROPY_PY_RANDOM_SEED_INIT_FUNC
#include <stddef.h>
void mp_hal_get_random(size_t n, void *buf);
static inline unsigned long mp_random_seed_init(void) {
    unsigned long r;
    mp_hal_get_random(sizeof(r), &r);
    return r;
}
#endif

#ifndef _DIRENT_HAVE_D_TYPE
#define _DIRENT_HAVE_D_TYPE (1)
#endif
#ifndef _DIRENT_HAVE_D_INO
#define _DIRENT_HAVE_D_INO (1)
#endif

#ifndef __APPLE__
#include <stdio.h>
#endif

// Configure the implementation of machine.idle().
#ifdef __wasi__
#define MICROPY_UNIX_MACHINE_IDLE
#else
#include <sched.h>
#define MICROPY_UNIX_MACHINE_IDLE sched_yield();
#endif

#ifndef MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE
#define MICROPY_PY_BLUETOOTH_ENABLE_CENTRAL_MODE (1)
#endif

#ifndef MICROPY_PY_BLUETOOTH_ENABLE_L2CAP_CHANNELS
#define MICROPY_PY_BLUETOOTH_ENABLE_L2CAP_CHANNELS (MICROPY_BLUETOOTH_NIMBLE)
#endif

// ── Abort-resume protocol ──
// The frame loop halts the VM via mp_sched_vm_abort() and resumes by
// calling mp_execute_bytecode() again with the same code_state.
// Two pointer saves in HOOK_LOOP make this safe — ip/sp are written
// to code_state (on pystack, in port_mem) before the abort can fire.

// mp_hal_delay_ms is overridden (see below) to call wasm_wfe once,
// which stores the sleep deadline and sets vm_abort.  The VM dispatch
// loop then fires nlr_jump_abort at the next mp_handle_pending check.

// WFE (Wait For Event) — the VM calls this when idle (asyncio sleep,
// time.sleep via mp_event_wait_ms, blocking I/O).
void wasm_wfe(int timeout_ms);
#define MICROPY_INTERNAL_WFE(timeout_ms) wasm_wfe(timeout_ms)

// ── VM hooks ──
//
// HOOK_LOOP: saves ip/sp to code_state, then checks budget.
//   The two stores ensure code_state is always resumable if
//   nlr_jump_abort fires from mp_handle_pending.
//
// HOOK_RETURN: drains background callbacks.
//
// Frame model:
//   port_frame → drain events + callbacks (before VM)
//   VM burst   → HOOK_LOOP saves ip/sp, checks budget
//   return     → HOOK_RETURN drains callbacks
//   JS idle    → UI rendering, user input
extern void wasm_vm_hook_loop(void);
#define MICROPY_VM_HOOK_LOOP \
    code_state->ip = ip; \
    code_state->sp = sp; \
    wasm_vm_hook_loop();

extern void background_callback_run_all(void);
#define RUN_BACKGROUND_TASKS    (background_callback_run_all())
#define MICROPY_VM_HOOK_RETURN  RUN_BACKGROUND_TASKS;

// Override mp_hal_delay_ms — cooperative yield instead of busy-wait.
// wasm_wfe stores the deadline in port_mem and calls mp_sched_vm_abort.
// The VM dispatch loop sees the abort flag at the next mp_handle_pending
// and fires nlr_jump_abort.  step_code gates VM re-entry on the deadline.
void mp_hal_delay_ms(mp_uint_t ms);
#define mp_hal_delay_ms mp_hal_delay_ms
