/*
 * WASM thread port — single-threaded, yield-aware.
 *
 * WASM runs one thread. But CircuitPython always "runs a thread" — the
 * Python VM is a thread that the supervisor starts and stops. The atomic
 * section API maps to yield prevention: while inside an atomic section,
 * the frame-budget supervisor must not trigger a VM yield.
 *
 * This header replaces the unix port's pthread-based mpthreadport.h.
 * The mutex types become simple integers (nesting counters or no-ops).
 */

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
// knows not to yield while inside one.  On real boards this disables
// interrupts; on WASM it prevents the frame-budget yield.
void mp_thread_begin_atomic_section(void);
void mp_thread_end_atomic_section(void);

// Query whether we're inside an atomic section (yield-prevention).
// The supervisor's budget check should call this before triggering yield.
bool mp_thread_in_atomic_section(void);
