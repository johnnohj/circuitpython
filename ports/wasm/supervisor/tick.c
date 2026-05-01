// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/tick.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// supervisor/tick.c — Port-local tick system for WASM.
//
// Two-level tick design matching upstream supervisor/shared/tick.c:
//
//   supervisor_tick() — LIGHTWEIGHT, called at ~1ms from port_background_task
//     Checks dirty flags, kicks lightweight module ticks, SCHEDULES
//     heavyweight work as a background callback.  Must be cheap.
//
//   supervisor_background_tick() — HEAVYWEIGHT, runs from callback queue
//     Queued by supervisor_tick(), executes when background_callback_
//     run_all() drains.  This is where displayio_background() runs.
//
// Design refs:
//   design/wasm-layer.md                    (Option A, timing primitives)
//   design/behavior/06-runtime-environments.md  (frame budget)

#include "supervisor/shared/tick.h"
#include "supervisor/background_callback.h"
#include "supervisor/port.h"

#include "mpthreadport.h"

#if CIRCUITPY_DISPLAYIO
#include "shared-module/displayio/__init__.h"
#endif

// ── Tick state ──

static background_callback_t tick_callback;
static volatile uint64_t last_finished_tick = 0;
static volatile size_t tick_enable_count = 0;

// ── supervisor_background_tick — HEAVYWEIGHT ──

static void supervisor_background_tick(void *unused) {
    (void)unused;

    #if CIRCUITPY_DISPLAYIO
    displayio_background();
    #endif

    last_finished_tick = port_get_raw_ticks(NULL);
}

// ── supervisor_tick — LIGHTWEIGHT ──

void supervisor_tick(void) {
    // Lightweight module ticks go here (keypad_tick, filesystem_tick, etc.)
    // Schedule heavyweight work as a callback.
    background_callback_add(&tick_callback, supervisor_background_tick, NULL);
}

bool supervisor_background_ticks_ok(void) {
    return port_get_raw_ticks(NULL) - last_finished_tick < 1024;
}

// ── Timing — derive from port_get_raw_ticks ──

static uint64_t _get_raw_subticks(void) {
    uint64_t ticks;
    uint8_t subticks;
    ticks = port_get_raw_ticks(&subticks);
    return (ticks << 5) | subticks;
}

uint64_t supervisor_ticks_ms64(void) {
    uint64_t result = port_get_raw_ticks(NULL);
    result = result * 1000 / 1024;
    return result;
}

uint32_t supervisor_ticks_ms32(void) {
    return (uint32_t)supervisor_ticks_ms64();
}

// ── supervisor_ticks_ms — mp_obj_t version for asyncio ──
// asyncio defines: #define ticks() supervisor_ticks_ms()

#include "py/obj.h"

mp_obj_t supervisor_ticks_ms(void) {
    uint64_t ticks = supervisor_ticks_ms64();
    return mp_obj_new_int((ticks + 0x1fff0000) % (1 << 29));
}

// ── Tick enable/disable ──

void supervisor_enable_tick(void) {
    mp_thread_begin_atomic_section();
    if (tick_enable_count == 0) {
        port_enable_tick();
    }
    tick_enable_count++;
    mp_thread_end_atomic_section();
}

void supervisor_disable_tick(void) {
    mp_thread_begin_atomic_section();
    if (tick_enable_count > 0) {
        tick_enable_count--;
    }
    if (tick_enable_count == 0) {
        port_disable_tick();
    }
    mp_thread_end_atomic_section();
}

// ── mp_hal_delay_ms — Option A ──
// Adopted from upstream supervisor/shared/tick.c.
// We provide this here because we replace the upstream tick.c entirely.
// The delay loop runs background tasks, checks for Ctrl-C, and yields
// via port_idle_until_interrupt() (abort-resume back to JS).
//
// Design refs:
//   design/wasm-layer.md  (Option A: adopt upstream supervisor delay)

// mp_hal_delay_ms is overridden in port/vm_abort.c — cooperative
// WFE instead of busy-wait.  See mpconfigport.h.
