// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/mpthreadport.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// mpthreadport.h — WASM thread port: single-threaded, yield-aware.
//
// WASM runs one thread.  But CircuitPython always "runs a thread" — the
// Python VM is a thread that the supervisor starts and stops.  The atomic
// section API maps to yield prevention: while inside an atomic section,
// the frame-budget supervisor must not trigger a VM abort.
//
// This header replaces the unix port's pthread-based mpthreadport.h.
// The mutex types become simple integers (nesting counters or no-ops).
//
// Design refs:
//   design/wasm-layer.md  (wasm layer)

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Mutex types — single-threaded, so these are no-ops structurally.
// We keep the types so code that references them still compiles.
typedef int mp_thread_mutex_t;
typedef int mp_thread_recursive_mutex_t;

void mp_thread_init(void);
void mp_thread_deinit(void);
void mp_thread_gc_others(void);

// Atomic sections — these track nesting depth so the supervisor
// knows not to abort while inside one.  On real boards this disables
// interrupts; on WASM it prevents the frame-budget abort.
void mp_thread_begin_atomic_section(void);
void mp_thread_end_atomic_section(void);

// Query whether we're inside an atomic section (abort-prevention).
// The budget check in wasm_vm_hook_loop should call this before aborting.
bool mp_thread_in_atomic_section(void);
