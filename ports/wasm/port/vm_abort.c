// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/vm_abort.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/vm_abort.c — VM abort-resume protocol.
//
// Implements the functions that halt the VM and return control to JS:
//
//   wasm_vm_hook_loop()        — budget check at every backwards branch
//   wasm_wfe()                 — MICROPY_INTERNAL_WFE: asyncio/event idle
//   port_idle_until_interrupt() — supervisor delay loop idle point (Option A)
//
// All three trigger mp_sched_vm_abort() which fires nlr_jump_abort() at
// the next mp_handle_pending() call.  The abort lands at the nlr_set_abort
// point in the frame function.
//
// By the time mp_sched_vm_abort() is called, MICROPY_VM_HOOK_LOOP has
// already saved ip/sp to code_state (see mpconfigport.h).  The C stack
// is destroyed by nlr_jump_abort, but code_state is on pystack (in
// port_mem), so resume is safe.
//
// Design refs:
//   design/wasm-layer.md                    (Option A: adopt upstream supervisor)
//   design/behavior/04-script-execution.md  (VM execution model)
//   design/behavior/06-runtime-environments.md  (frame budget)

#include <stdint.h>
#include "py/mpconfig.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "port/port_memory.h"
#include "port/budget.h"

// ── wasm_vm_hook_loop ──
//
// Called at every backwards branch in vm.c via MICROPY_VM_HOOK_LOOP.
//
// IMPORTANT: ip/sp have ALREADY been saved to code_state by the
// MICROPY_VM_HOOK_LOOP macro before this function is called.
// See mpconfigport.h:
//   #define MICROPY_VM_HOOK_LOOP \
//       code_state->ip = ip; \
//       code_state->sp = sp; \
//       wasm_vm_hook_loop();

void wasm_vm_hook_loop(void) {
    #if MICROPY_ENABLE_VM_ABORT
    if (budget_soft_expired()) {
        port_mem.vm_abort_reason = VM_ABORT_BUDGET;
        mp_sched_vm_abort();
    }
    #endif
}

// ── wasm_wfe ──
//
// Wait For Event.  Called from mp_event_wait_ms / mp_event_wait_indefinite
// via MICROPY_INTERNAL_WFE when the VM is idle (asyncio sleep, blocking I/O).
//
// Stores the wakeup deadline in port_mem so JS knows when to schedule
// the next frame, then triggers abort to return to JS.

void wasm_wfe(int timeout_ms) {
    #if MICROPY_ENABLE_VM_ABORT
    if (timeout_ms < 0) {
        port_mem.wakeup_ms = 0;
    } else {
        port_mem.wakeup_ms = mp_hal_ticks_ms() + (uint32_t)timeout_ms;
    }
    port_mem.vm_abort_reason = VM_ABORT_WFE;
    // Set the flag — don't call nlr_jump_abort() directly.
    // Let the C stack unwind normally back to the VM dispatch loop,
    // where mp_handle_pending() will fire nlr_jump_abort at a point
    // where ip/sp are correct (saved by HOOK_LOOP at the backwards
    // branch).  Calling nlr_jump_abort() from here would destroy
    // the C stack from inside a C function call, losing context.
    mp_sched_vm_abort();
    #else
    (void)timeout_ms;
    #endif
}

// Override mp_hal_delay_ms — cooperative yield instead of busy-wait.
// wasm_wfe stores the wakeup deadline in port_mem and sets vm_abort.
// mp_hal_delay_ms returns, the VM dispatch loop checks mp_handle_pending,
// nlr_jump_abort fires, and step_code gates VM re-entry on the deadline.
void mp_hal_delay_ms(mp_uint_t ms) {
    wasm_wfe((int)ms);
}

// ── port_idle_until_interrupt ──
//
// Called by the upstream supervisor's mp_hal_delay_ms loop in
// supervisor/shared/tick.c (Option A).  On real boards, this puts
// the CPU to sleep until a timer interrupt wakes it.  For us,
// "sleep" = abort back to JS, "timer interrupt" = next frame.
//
// The supervisor has already set up port_interrupt_after_ticks()
// with the remaining delay.  We just need to yield.
//
// Unlike wasm_wfe, we don't set wakeup_ms here — the supervisor
// manages the deadline via port_interrupt_after_ticks().

void port_idle_until_interrupt(void) {
    #if MICROPY_ENABLE_VM_ABORT
    port_mem.vm_abort_reason = VM_ABORT_WFE;
    mp_sched_vm_abort();
    #endif
}
