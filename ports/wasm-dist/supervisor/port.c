/*
 * supervisor/port.c — WASM port supervisor implementation.
 *
 * Implements the port interface from supervisor/port.h.
 * Adapted from ports/wasm-wasi/supervisor/port.c.
 *
 * Key differences from hardware ports:
 *   - No timer ISR — soft tick via monotonic clock in port_background_task
 *   - No physical memory — heap is a static buffer in WASM linear memory
 *   - Atomic sections use mpthreadport.c's nesting counter
 *   - port_idle_until_interrupt: nanosleep in CLI mode, yield in browser
 */

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "mpthreadport.h"

/* ------------------------------------------------------------------ */
/* port_background_task — called by RUN_BACKGROUND_TASKS               */
/*                                                                     */
/* On a real board, a 1ms hardware timer ISR calls supervisor_tick().   */
/* On WASM, JS calls cp_step() once per rAF (~60fps).  We call         */
/* supervisor_tick() once per frame to queue background work.           */
/* Time-dependent code reads CLOCK_MONOTONIC directly via              */
/* port_get_raw_ticks() / supervisor_ticks_ms64().                     */
/* ------------------------------------------------------------------ */

extern void supervisor_tick(void);

void port_background_task(void) {
    supervisor_tick();
}

/* ------------------------------------------------------------------ */
/* Tick control                                                        */
/* ------------------------------------------------------------------ */

/* CircuitPython ticks are 1/1024 second (~0.977ms). */
uint64_t port_get_raw_ticks(uint8_t *subticks) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t total_us = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    uint64_t ticks = (total_us * 1024) / 1000000;
    if (subticks) {
        *subticks = 0;
    }
    return ticks;
}

void port_enable_tick(void) {
    /* No-op: background tick runs unconditionally once per frame. */
}

void port_disable_tick(void) {
    /* No-op: background tick runs unconditionally once per frame. */
}

void port_interrupt_after_ticks(uint32_t ticks) {
    (void)ticks;
}

void port_idle_until_interrupt(void) {
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/* Background tick hooks                                               */
/* ------------------------------------------------------------------ */

void port_background_tick(void) {
    /* Port-specific periodic work inside supervisor_background_tick. */
}

void port_start_background_tick(void) {
}

void port_finish_background_tick(void) {
}

/* ------------------------------------------------------------------ */
/* Port heap (used by supervisor for non-GC allocations)               */
/* ------------------------------------------------------------------ */

void *port_malloc(size_t size, bool dma_capable) {
    (void)dma_capable;
    return malloc(size);
}

void port_free(void *ptr) {
    free(ptr);
}

size_t gc_get_max_new_split(void) {
    return 0;
}

/* ------------------------------------------------------------------ */
/* GC collection — port-specific roots                                 */
/* ------------------------------------------------------------------ */

void port_gc_collect(void) {
    /* No port-specific GC roots */
}

/* ------------------------------------------------------------------ */
/* Interrupt control                                                   */
/*                                                                     */
/* Used by py/scheduler.c and background_callback.c.                   */
/* Maps to mpthreadport.c's atomic section counter so the frame-budget */
/* yield check respects these critical regions.                        */
/* ------------------------------------------------------------------ */

void common_hal_mcu_disable_interrupts(void) {
    mp_thread_unix_begin_atomic_section();
}

void common_hal_mcu_enable_interrupts(void) {
    mp_thread_unix_end_atomic_section();
}

void common_hal_mcu_delay_us(uint32_t us) {
    struct timespec ts = {
        .tv_sec = us / 1000000,
        .tv_nsec = (us % 1000000) * 1000
    };
    nanosleep(&ts, NULL);
}
