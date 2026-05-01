// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/port.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// supervisor/port.c — supervisor/port.h contract implementation.
//
// Implements the functions that the CircuitPython supervisor expects
// every port to provide.  On real boards, these manage hardware
// peripherals, power, and memory.  On WASM, they manage port_mem
// and the abort-resume substrate.
//
// Functions split across wasm-layer files:
//   port_get_raw_ticks      → port/wasi_mphal.c (WASI clock)
//   port_interrupt_after_ticks → port/wasi_mphal.c
//   port_idle_until_interrupt → port/vm_abort.c (abort-resume)
//
// This file provides the remaining contract functions.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, supervisor contract)
//   design/behavior/01-hardware-init.md     (port_init, reset)
//   design/behavior/05-vm-lifecycle.md      (GC heap, pystack)

#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "py/runtime.h"
#include "py/gc.h"
#include "py/mphal.h"

#include "mpthreadport.h"
#include "port/port_memory.h"
#include "supervisor/port.h"

#if CIRCUITPY_DISPLAYIO
#include "shared-module/displayio/__init__.h"
#endif
#if CIRCUITPY_MICROCONTROLLER
#include "shared-bindings/microcontroller/Pin.h"
#endif
#if CIRCUITPY_BOARD_I2C || CIRCUITPY_BOARD_SPI || CIRCUITPY_BOARD_UART
#include "shared-module/board/__init__.h"
#endif

// ── supervisor_tick — called from port_background_task ──

extern void supervisor_tick(void);

// ── port_background_task ──
// Time-gates supervisor_tick() to ~1ms (one raw tick = 1/1024s).
// On upstream, supervisor_tick() is called from a 1ms SysTick ISR.
// We have no ISR, so this runs at every background_callback_run_all()
// and uses the clock to throttle.

static uint64_t _last_tick_ticks = 0;

void port_background_task(void) {
    uint64_t now = port_get_raw_ticks(NULL);
    if (now != _last_tick_ticks) {
        _last_tick_ticks = now;
        supervisor_tick();
    }
}

// ── Tick control ──
// On real boards, port_enable_tick starts a hardware timer.
// On WASM, ticks are software-driven from port_background_task.
// These are no-ops — the tick fires whenever background tasks run.
// TODO: Could set a flag that JS uses to adjust frame scheduling.

void port_enable_tick(void) {}
void port_disable_tick(void) {}

void port_background_tick(void) {}
void port_start_background_tick(void) {}
void port_finish_background_tick(void) {}

// ── Memory allocation ──
// Port-level malloc for non-GC allocations (e.g., display buffers).

void *port_malloc(size_t size, bool dma_capable) {
    (void)dma_capable;  // No DMA on WASM
    return malloc(size);
}

void port_free(void *ptr) {
    free(ptr);
}

// ── GC split heap ──
// We use a single contiguous heap in port_mem — no split heap.

size_t gc_get_max_new_split(void) {
    return 0;
}

void port_gc_collect(void) {}

// ── Interrupt control ──
// Maps to atomic section nesting (mpthreadport.c).
// On real boards, these mask/unmask hardware interrupts.
// On WASM, they prevent the abort-resume yield.

void common_hal_mcu_disable_interrupts(void) {
    mp_thread_begin_atomic_section();
}

void common_hal_mcu_enable_interrupts(void) {
    mp_thread_end_atomic_section();
}

void common_hal_mcu_delay_us(uint32_t us) {
    struct timespec ts = {
        .tv_sec = us / 1000000,
        .tv_nsec = (us % 1000000) * 1000
    };
    nanosleep(&ts, NULL);
}

// ── Port heap boundaries ──
// Used by the supervisor to set up the GC heap.

uint32_t *port_heap_get_bottom(void) {
    return (uint32_t *)port_gc_heap();
}

uint32_t *port_heap_get_top(void) {
    return (uint32_t *)(port_gc_heap() + port_gc_heap_size());
}

// ── Stack boundaries ──
// On real boards, these return stack pointer limits for overflow detection.
// On WASM, the runtime manages the stack — these return safe values.

uint32_t *port_stack_get_limit(void) {
    return (uint32_t *)0;
}

uint32_t *port_stack_get_top(void) {
    return (uint32_t *)0xFFFFFFFF;
}
