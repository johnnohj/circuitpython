/*
 * supervisor/tick.c — Port-local tick system for WASM.
 *
 * Two-level tick design matching upstream supervisor/shared/tick.c:
 *
 *   supervisor_tick()            — LIGHTWEIGHT, runs frequently
 *     Called from wasm_vm_hook_loop() at every backwards branch.
 *     Time-gated to ~1ms by port_background_task().
 *     Checks module dirty flags, kicks lightweight module ticks
 *     (keypad_tick, filesystem_tick), and SCHEDULES the heavyweight
 *     work as a background callback.  Must be cheap — this runs in
 *     the bytecode hot loop.
 *
 *     On upstream, this is the 1ms ISR body.  ISRs can't do heavy
 *     work (no heap alloc, no displayio), so they schedule it.
 *     We maintain the same contract even though WASM has no ISRs,
 *     because the call site (HOOK_LOOP) has the same constraint:
 *     it must be fast.
 *
 *   supervisor_background_tick() — HEAVYWEIGHT, runs at frame bookends
 *     Queued by supervisor_tick(), executes when background_callback_
 *     run_all() drains the queue.  That drain happens:
 *       - In cp_hw_step() (once per frame, before and after VM)
 *       - At MICROPY_VM_HOOK_RETURN (every function return)
 *     This is where displayio_background() and other expensive
 *     periodic work runs.  It has time — it's in the frame margin,
 *     not the bytecode hot path.
 *
 * The callback queue is the buffer between "something needs doing"
 * (scheduled by supervisor_tick) and "do it now" (drained at frame
 * boundaries).  This lets HOOK_LOOP call supervisor_tick() without
 * pulling in display refresh at every backwards branch.
 */

#include "supervisor/shared/tick.h"
#include "supervisor/background_callback.h"
#include "supervisor/port.h"

#include "mpthreadport.h"

#if CIRCUITPY_DISPLAYIO
#include "shared-module/displayio/__init__.h"
#endif

/* ------------------------------------------------------------------ */
/* Tick state                                                          */
/* ------------------------------------------------------------------ */

static background_callback_t tick_callback;
static volatile uint64_t last_finished_tick = 0;
static volatile size_t tick_enable_count = 0;

/* ------------------------------------------------------------------ */
/* supervisor_background_tick — HEAVYWEIGHT, runs from callback queue   */
/*                                                                     */
/* Scheduled by supervisor_tick(), executed when the callback queue     */
/* drains at frame boundaries (cp_hw_step) or function returns         */
/* (MICROPY_VM_HOOK_RETURN).  This is the safe place for expensive     */
/* periodic work — display refresh, filesystem flush, etc.             */
/* ------------------------------------------------------------------ */

static void supervisor_background_tick(void *unused) {
    (void)unused;

    #if CIRCUITPY_DISPLAYIO
    displayio_background();
    #endif

    /* Future: filesystem_background() when filesystem is wired */

    last_finished_tick = port_get_raw_ticks(NULL);
}

/* ------------------------------------------------------------------ */
/* supervisor_tick — LIGHTWEIGHT, called at ~1ms from HOOK_LOOP        */
/*                                                                     */
/* This is the equivalent of the upstream 1ms ISR.  It must be cheap:  */
/* check flags, kick lightweight module ticks, schedule heavy work.    */
/* No displayio, no filesystem flush, no heap allocation.              */
/*                                                                     */
/* On upstream, supervisor_tick() is called from the SysTick ISR and   */
/* does: filesystem_tick(), keypad_tick(), then enqueues                */
/* supervisor_background_tick as a callback.  We follow the same       */
/* pattern so modules added later (keypad, countio) just slot in.      */
/* ------------------------------------------------------------------ */

void supervisor_tick(void) {
    /* Lightweight module ticks go here.  These check dirty flags,
     * scan matrices, update counters — fast, no allocation.
     *
     * Future:
     *   #if CIRCUITPY_KEYPAD
     *   keypad_tick();
     *   #endif
     *
     *   #if CIRCUITPY_FILESYSTEM_FLUSH_INTERVAL_MS > 0
     *   filesystem_tick();
     *   #endif
     */

    /* Schedule the heavyweight work (displayio, filesystem flush)
     * as a callback.  It will execute at the next queue drain —
     * either in cp_hw_step() or at a function return. */
    background_callback_add(&tick_callback, supervisor_background_tick, NULL);
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
