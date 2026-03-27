/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
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

// Platform compiler string for banner — defined directly to avoid
// include-order issues with extmod/modplatform.h
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

// CIRCUITPY-CHANGE
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
// Some compilers define __thumb2__ and __arm__ at the same time, let
// autodetected thumb2 emitter have priority.
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
// unless support for it is disabled.
#if !defined(MICROPY_NO_ALLOCA) || MICROPY_NO_ALLOCA == 0
#if defined(__FreeBSD__) || defined(__NetBSD__)
#include <stdlib.h>
#else
#include <alloca.h>
#endif
#endif

// Always enable GC.
#define MICROPY_ENABLE_GC           (1)
// CIRCUITPY-CHANGE
#define MICROPY_ENABLE_SELECTIVE_COLLECT (1)

#if !(defined(MICROPY_GCREGS_SETJMP) || defined(__x86_64__) || defined(__i386__) || defined(__thumb2__) || defined(__thumb__) || defined(__arm__) || (defined(__riscv) && (__riscv_xlen == 64)))
// Fall back to setjmp() implementation for discovery of GC pointers in registers.
#define MICROPY_GCREGS_SETJMP (1)
#endif

// Enable the VFS, and enable the posix "filesystem".
#define MICROPY_ENABLE_FINALISER    (1)
#define MICROPY_VFS                 (1)
#define MICROPY_READER_VFS          (1)
#define MICROPY_HELPER_LEXER_UNIX   (1)
#define MICROPY_VFS_POSIX           (1)
#define MICROPY_READER_POSIX        (1)
// CIRCUITPY-CHANGE: define no matter what
#ifndef MICROPY_TRACKED_ALLOC
#define MICROPY_TRACKED_ALLOC       (MICROPY_PY_FFI || MICROPY_BLUETOOTH_BTSTACK)
#endif

// VFS stat functions should return time values relative to 1970/1/1
#define MICROPY_EPOCH_IS_1970       (1)

// Assume that select() call, interrupted with a signal, and erroring
// with EINTR, updates remaining timeout value.
#define MICROPY_SELECT_REMAINING_TIME (1)

// Disable stackless by default.
#ifndef MICROPY_STACKLESS
#define MICROPY_STACKLESS           (0)
#define MICROPY_STACKLESS_STRICT    (0)
#endif

// Implementation of the machine module.
#ifdef __wasi__
// WASM: linear memory is directly addressable — no /dev/mem needed.
#define MICROPY_PY_MACHINE_INCLUDEFILE "ports/wasm-dist/modmachine.c"
#define MICROPY_MACHINE_MEM_GET_READ_ADDR   mod_machine_mem_get_addr
#define MICROPY_MACHINE_MEM_GET_WRITE_ADDR  mod_machine_mem_get_addr
#else
#define MICROPY_PY_MACHINE_INCLUDEFILE "ports/unix/modmachine.c"
#define MICROPY_MACHINE_MEM_GET_READ_ADDR   mod_machine_mem_get_addr
#define MICROPY_MACHINE_MEM_GET_WRITE_ADDR  mod_machine_mem_get_addr
#endif

#define MICROPY_FATFS_ENABLE_LFN       (1)
#define MICROPY_FATFS_RPATH            (2)
#define MICROPY_FATFS_MAX_SS           (4096)
#define MICROPY_FATFS_LFN_CODE_PAGE    437 /* 1=SFN/ANSI 437=LFN/U.S.(OEM) */
// CIRCUITPY-CHANGE: enable FAT32 support
#define MICROPY_FATFS_MKFS_FAT32       (1)
// CIRCUITPY-CHANGE: allow FAT label access
#define MICROPY_FATFS_USE_LABEL (1)

#define MICROPY_ALLOC_PATH_MAX      (PATH_MAX)

// Ensure builtinimport.c works with -m.
#define MICROPY_MODULE_OVERRIDE_MAIN_IMPORT (1)

// Don't default sys.argv and sys.path because we do that in main.
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS (0)

// Enable sys.executable.
#define MICROPY_PY_SYS_EXECUTABLE (1)

#ifndef __wasi__
#define MICROPY_PY_SOCKET_LISTEN_BACKLOG_DEFAULT (SOMAXCONN < 128 ? SOMAXCONN : 128)
#endif

// Bare-metal ports don't have stderr. Printing debug to stderr may give tests
// which check stdout a chance to pass, etc.
extern const struct _mp_print_t mp_stderr_print;
#define MICROPY_DEBUG_PRINTER (&mp_stderr_print)
#define MICROPY_ERROR_PRINTER (&mp_stderr_print)

// For the native emitter configure how to mark a region as executable.
#ifndef __wasi__
void mp_unix_alloc_exec(size_t min_size, void **ptr, size_t *size);
void mp_unix_free_exec(void *ptr, size_t size);
#define MP_PLAT_ALLOC_EXEC(min_size, ptr, size) mp_unix_alloc_exec(min_size, ptr, size)
#define MP_PLAT_FREE_EXEC(ptr, size) mp_unix_free_exec(ptr, size)
#endif

// If enabled, configure how to seed random on init.
#ifdef MICROPY_PY_RANDOM_SEED_INIT_FUNC
#include <stddef.h>
void mp_hal_get_random(size_t n, void *buf);
static inline unsigned long mp_random_seed_init(void) {
    unsigned long r;
    mp_hal_get_random(sizeof(r), &r);
    return r;
}
#endif

#ifdef __linux__
// Can access physical memory using /dev/mem
#define MICROPY_PLAT_DEV_MEM  (1)
#endif

#ifdef __ANDROID__
#include <android/api-level.h>
#if __ANDROID_API__ < 4
// Bionic libc in Android 1.5 misses these 2 functions
#define MP_NEED_LOG2 (1)
#define nan(x) NAN
#endif
#endif

// From "man readdir": "Under glibc, programs can check for the availability
// of the fields [in struct dirent] not defined in POSIX.1 by testing whether
// the macros [...], _DIRENT_HAVE_D_TYPE are defined."
// Other libc's don't define it, but proactively assume that dirent->d_type
// is available on a modern *nix system.
#ifndef _DIRENT_HAVE_D_TYPE
#define _DIRENT_HAVE_D_TYPE (1)
#endif
// This macro is not provided by glibc but we need it so ports that don't have
// dirent->d_ino can disable the use of this field.
#ifndef _DIRENT_HAVE_D_INO
#define _DIRENT_HAVE_D_INO (1)
#endif

#ifndef __APPLE__
// For debugging purposes, make printf() available to any source file.
#include <stdio.h>
#endif

// Configure the implementation of machine.idle().
#ifdef __wasi__
// WASI has no sched_yield — idle is a no-op (or future yield-to-JS)
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

// ── VM yield protocol ──
// When enabled, the VM checks mp_vm_should_yield() at backwards branches
// and returns MP_VM_RETURN_YIELD with state saved for resumption.
#if MICROPY_VM_YIELD_ENABLED
extern int mp_vm_should_yield(void);
extern void *mp_vm_yield_state;
#define MICROPY_VM_YIELD_SAVE_STATE(cs) do { mp_vm_yield_state = (void *)(cs); } while (0)

// Override mp_hal_delay_ms — yield-as-sleep instead of busy-wait.
// The #define prevents unix_mphal.c from providing a blocking version.
void mp_hal_delay_ms(mp_uint_t ms);
#define mp_hal_delay_ms mp_hal_delay_ms
#endif

// ── VM hook: background tasks + budget check at every branch ──
// MICROPY_VM_HOOK_LOOP runs at backwards branches in py/vm.c, immediately
// before the MICROPY_VM_YIELD_ENABLED check.  This is where we service
// background tasks (display, hw endpoints, Ctrl-C) and check the wall-clock
// budget.  When budget is expired, we call mp_vm_request_yield() so the
// yield check right after will save state and return.
#if MICROPY_VM_YIELD_ENABLED
extern void wasm_vm_hook_loop(void);
#define MICROPY_VM_HOOK_LOOP    wasm_vm_hook_loop();
#else
#define MICROPY_VM_HOOK_LOOP
#endif

// CIRCUITPY-CHANGE: wire to port-local background_callback_run_all()
extern void background_callback_run_all(void);
#define RUN_BACKGROUND_TASKS (background_callback_run_all())
