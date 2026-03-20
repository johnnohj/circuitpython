/*
 * supervisor/port.c — WASI port supervisor implementation
 *
 * Implements the critical functions from supervisor/port.h.
 * Non-critical functions use the weak defaults from supervisor/shared/port.c.
 *
 * Key difference from hardware ports: no timer ISR. Instead,
 * port_background_task() checks the monotonic clock and calls
 * supervisor_tick() when 1ms has elapsed. This drives the entire
 * background tick system (filesystem flush, display refresh, etc.)
 * through the standard CircuitPython infrastructure.
 */

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "py/runtime.h"
#include "supervisor/port.h"
#include "supervisor/shared/tick.h"
#include "supervisor/shared/safe_mode.h"

// ---- Critical section for background_callback ----
// WASI is single-threaded but the critical section prevents re-entrant
// callback list manipulation. Defined as CALLBACK_CRITICAL_BEGIN/END
// in mpconfigboard.h to avoid pulling in microcontroller headers.

static volatile int _critical_depth = 0;

void wasi_critical_begin(void) {
    _critical_depth++;
}

void wasi_critical_end(void) {
    if (_critical_depth > 0) {
        _critical_depth--;
    }
}

// ---- Static buffers ----

// GC heap (fixed address for pointer stability across checkpoints)
#ifndef WASI_GC_HEAP_SIZE
#define WASI_GC_HEAP_SIZE (256 * 1024)
#endif

static char _heap[WASI_GC_HEAP_SIZE];

// Stack bounds (WASM shadow stack — approximated)
static uint32_t _stack_top;
static uint32_t _stack_limit;

// Saved word for safe mode persistence
static uint32_t _saved_word = 0;

// Tick tracking — simulates the hardware timer ISR
static uint64_t _last_tick_ms = 0;
static bool _ticks_enabled = false;

// ---- Helpers ----

static uint64_t _monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---- INITIALIZATION ----

safe_mode_t port_init(void) {
    _last_tick_ms = _monotonic_ms();

    // Capture stack bounds from a local variable address
    uint32_t stack_var;
    _stack_top = (uint32_t)(uintptr_t)&stack_var;
    _stack_limit = _stack_top - (16 * 1024);  // 16KB stack

    return SAFE_MODE_NONE;
}

void reset_port(void) {
    // Nothing to reset for WASI
}

void reset_cpu(void) {
    exit(0);
}

void reset_to_bootloader(void) {
    exit(0);
}

// ---- STACK AND HEAP ----

uint32_t *port_stack_get_limit(void) {
    return (uint32_t *)(uintptr_t)_stack_limit;
}

uint32_t *port_stack_get_top(void) {
    return (uint32_t *)(uintptr_t)_stack_top;
}

uint32_t *port_heap_get_bottom(void) {
    return (uint32_t *)_heap;
}

uint32_t *port_heap_get_top(void) {
    return (uint32_t *)(_heap + WASI_GC_HEAP_SIZE);
}

void port_set_saved_word(uint32_t value) {
    _saved_word = value;
}

uint32_t port_get_saved_word(void) {
    return _saved_word;
}

// ---- TICKS ----
// CircuitPython ticks are 1/1024 second (~0.977ms).
// We use CLOCK_MONOTONIC and convert.

uint64_t port_get_raw_ticks(uint8_t *subticks) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // Convert to 1/1024 second ticks
    uint64_t total_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    uint64_t ticks = (total_us * 1024) / 1000000;
    if (subticks) {
        *subticks = 0;
    }
    return ticks;
}

void port_enable_tick(void) {
    _ticks_enabled = true;
}

void port_disable_tick(void) {
    _ticks_enabled = false;
}

void port_interrupt_after_ticks(uint32_t ticks) {
    // WASI has no interrupt mechanism — tick polling in port_background_task
    (void)ticks;
}

void port_idle_until_interrupt(void) {
    // Sleep 1ms — equivalent to waiting for next tick
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
    nanosleep(&ts, NULL);
}

// ---- BACKGROUND TASKS ----

void port_background_task(void) {
    // Simulate the hardware timer ISR: check if 1ms has elapsed
    // and call supervisor_tick() if so. This drives the entire
    // background tick system (filesystem_tick, background_callback queue).
    if (_ticks_enabled) {
        uint64_t now = _monotonic_ms();
        while (now > _last_tick_ms) {
            _last_tick_ms++;
            supervisor_tick();
        }
    }
}

// Defined in main_opfs.c — services OPFS device files
extern void opfs_background_tick(void);

void port_background_tick(void) {
    // Port-specific periodic work (called inside supervisor_background_tick).
    // Services OPFS device files: flush /dev/repl, check Ctrl-C, checkpoint state.
    opfs_background_tick();
}

void port_start_background_tick(void) {
    // No debug pin toggling on WASI
}

void port_finish_background_tick(void) {
    // No debug pin toggling on WASI
}
