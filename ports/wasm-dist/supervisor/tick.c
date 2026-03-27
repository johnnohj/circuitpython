/*
 * supervisor/tick.c — Port-local tick system for WASM.
 *
 * Adapted from supervisor/shared/tick.c.
 * Same interface (tick.h), WASM-specific changes:
 *   - No BLE HCI, keypad, watchdog, filesystem tick
 *   - No displayio_background() yet (stubbed)
 *   - supervisor_background_tick queued via background_callback
 *   - supervisor_ticks_ms64/32 derive from port_get_raw_ticks
 *   - mp_hal_delay_ms NOT defined here (vm_yield.c provides yield-based version)
 *   - supervisor_enable/disable_tick use port_enable/disable_tick
 *
 * supervisor_tick() is called by port_background_task() in port.c
 * once per elapsed millisecond (soft-simulated from CLOCK_MONOTONIC).
 */

#include "supervisor/shared/tick.h"
#include "supervisor/background_callback.h"
#include "supervisor/port.h"
#include "supervisor/shared/stack.h"

#include "mpthreadport.h"

/* ------------------------------------------------------------------ */
/* Tick state                                                          */
/* ------------------------------------------------------------------ */

static background_callback_t tick_callback;
static volatile uint64_t last_finished_tick = 0;
static volatile size_t tick_enable_count = 0;

/* ------------------------------------------------------------------ */
/* supervisor_background_tick — tier 2 work, runs from callback queue  */
/* ------------------------------------------------------------------ */

static void supervisor_background_tick(void *unused) {
    (void)unused;

    port_start_background_tick();

    assert_heap_ok();

    /* TODO: displayio_background() when display is wired */
    /* TODO: filesystem_background() when filesystem is wired */

    port_background_tick();

    assert_heap_ok();

    last_finished_tick = port_get_raw_ticks(NULL);

    port_finish_background_tick();
}

bool supervisor_background_ticks_ok(void) {
    return port_get_raw_ticks(NULL) - last_finished_tick < 1024;
}

/* ------------------------------------------------------------------ */
/* supervisor_tick — called once per ms by port_background_task()      */
/* ------------------------------------------------------------------ */

void supervisor_tick(void) {
    /* Queue supervisor_background_tick to run in background_callback_run_all */
    background_callback_add(&tick_callback, supervisor_background_tick, NULL);
}

/* ------------------------------------------------------------------ */
/* Timing — derive from port_get_raw_ticks (1/1024 second resolution) */
/* ------------------------------------------------------------------ */

uint64_t supervisor_ticks_ms64(void) {
    uint64_t result = port_get_raw_ticks(NULL);
    result = result * 1000 / 1024;
    return result;
}

uint32_t supervisor_ticks_ms32(void) {
    return (uint32_t)supervisor_ticks_ms64();
}

/* ------------------------------------------------------------------ */
/* supervisor_ticks_ms — mp_obj_t version for asyncio                  */
/*                                                                     */
/* asyncio defines: #define ticks() supervisor_ticks_ms()               */
/* Returns mp_obj_t with 29-bit wrapping value.                        */
/* ------------------------------------------------------------------ */

#include "py/obj.h"

mp_obj_t supervisor_ticks_ms(void) {
    uint64_t ticks = supervisor_ticks_ms64();
    return mp_obj_new_int((ticks + 0x1fff0000) % (1 << 29));
}

/* ------------------------------------------------------------------ */
/* Tick enable/disable — control port timer                            */
/* ------------------------------------------------------------------ */

void supervisor_enable_tick(void) {
    mp_thread_unix_begin_atomic_section();
    if (tick_enable_count == 0) {
        port_enable_tick();
    }
    tick_enable_count++;
    mp_thread_unix_end_atomic_section();
}

void supervisor_disable_tick(void) {
    mp_thread_unix_begin_atomic_section();
    if (tick_enable_count > 0) {
        tick_enable_count--;
    }
    if (tick_enable_count == 0) {
        port_disable_tick();
    }
    mp_thread_unix_end_atomic_section();
}
