/*
 * WASM thread port — single-threaded, yield-aware.
 *
 * Provides the threading API contract that MicroPython expects, adapted
 * for a single-threaded WASM environment where "threading" means the
 * supervisor starting/stopping the Python VM.
 *
 * Key adaptation: atomic sections track nesting depth.  The frame-budget
 * supervisor checks mp_thread_in_atomic_section() before triggering a
 * VM yield — if we're inside an atomic section, the yield is deferred
 * until the section ends.  This prevents state corruption during
 * operations that must not be interrupted (GC, scheduler updates, etc.).
 *
 * On the unix port, atomic sections are pthread recursive mutex locks.
 * On real boards, they disable interrupts.  Here, they're a counter
 * that the cooperative yield respects.
 */

#include <stdio.h>

#include "py/runtime.h"
#include "py/mpthread.h"
#include "py/gc.h"

// ---- Atomic section nesting ----
// Tracks depth so nested begin/end pairs work correctly.
// The supervisor's mp_vm_should_yield() should check this.
static volatile int _atomic_depth = 0;

void mp_thread_unix_begin_atomic_section(void) {
    _atomic_depth++;
}

void mp_thread_unix_end_atomic_section(void) {
    if (_atomic_depth > 0) {
        _atomic_depth--;
    }
}

bool mp_thread_in_atomic_section(void) {
    return _atomic_depth > 0;
}

// ---- Threading API ----
// Only compiled when MICROPY_PY_THREAD is enabled.
// The full threading API (lifecycle, mutexes, GC) lives behind this guard
// because py/mpthread.h only exposes these declarations when threading is on.

#if MICROPY_PY_THREAD

// ---- Thread lifecycle ----
// Single thread — init/deinit are bookkeeping only.

void mp_thread_init(void) {
    _atomic_depth = 0;
}

void mp_thread_deinit(void) {
    _atomic_depth = 0;
}

// ---- Thread state ----
// Single thread uses the global mp_state_ctx directly.

mp_state_thread_t *mp_thread_get_state(void) {
    return &mp_state_ctx.thread;
}

void mp_thread_set_state(mp_state_thread_t *state) {
    (void)state;
}

mp_uint_t mp_thread_get_id(void) {
    return 1;
}

// ---- GC ----
// No other threads to scan.

void mp_thread_gc_others(void) {
}

// ---- Mutexes ----
// Single-threaded — all locks succeed immediately.

void mp_thread_mutex_init(mp_thread_mutex_t *mutex) {
    *mutex = 0;
}

int mp_thread_mutex_lock(mp_thread_mutex_t *mutex, int wait) {
    (void)wait;
    *mutex = 1;
    return 1;
}

void mp_thread_mutex_unlock(mp_thread_mutex_t *mutex) {
    *mutex = 0;
}

#if MICROPY_PY_THREAD_RECURSIVE_MUTEX

void mp_thread_recursive_mutex_init(mp_thread_recursive_mutex_t *mutex) {
    *mutex = 0;
}

int mp_thread_recursive_mutex_lock(mp_thread_recursive_mutex_t *mutex, int wait) {
    (void)wait;
    (*mutex)++;
    return 1;
}

void mp_thread_recursive_mutex_unlock(mp_thread_recursive_mutex_t *mutex) {
    if (*mutex > 0) {
        (*mutex)--;
    }
}

#endif // MICROPY_PY_THREAD_RECURSIVE_MUTEX
#endif // MICROPY_PY_THREAD
