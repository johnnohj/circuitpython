/*
 * supervisor/tick.c — Port-local tick system for WASM.
 *
 * Adapted from supervisor/shared/tick.c.
 * Same interface (tick.h), WASM-specific changes:
 *   - No BLE HCI, keypad, watchdog, filesystem tick
 *   - No ISR → no callback-queue indirection (direct call)
 *   - supervisor_ticks_ms64/32 derive from port_get_raw_ticks
 *   - mp_hal_delay_ms NOT defined here (vm_yield.c provides yield-based version)
 *   - supervisor_enable/disable_tick use port_enable/disable_tick
 *
 * supervisor_tick() is called by port_background_task() in port.c,
 * time-gated to ~1ms via port_get_raw_ticks().
 */

#include "supervisor/shared/tick.h"
#include "supervisor/port.h"

#include "mpthreadport.h"

#if CIRCUITPY_DISPLAYIO
#include "shared-module/displayio/__init__.h"
#endif

/* ------------------------------------------------------------------ */
/* Tick state                                                          */
/* ------------------------------------------------------------------ */

static volatile uint64_t last_finished_tick = 0;
static volatile size_t tick_enable_count = 0;

/* ------------------------------------------------------------------ */
/* supervisor_tick — called ~1ms by port_background_task()             */
/*                                                                     */
/* On upstream, supervisor_tick() is called from a 1ms ISR and enqueues */
/* supervisor_background_tick as a callback for later draining.  The   */
/* queue decouples ISR context (can't call displayio) from main context */
/* (can).  WASM has no ISR — everything runs in main context — so we   */
/* call the tick work directly, avoiding the round-trip.               */
/* ------------------------------------------------------------------ */

void supervisor_tick(void) {
    #if CIRCUITPY_DISPLAYIO
    displayio_background();
    #endif

    last_finished_tick = port_get_raw_ticks(NULL);
}

bool supervisor_background_ticks_ok(void) {
    return port_get_raw_ticks(NULL) - last_finished_tick < 1024;
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
