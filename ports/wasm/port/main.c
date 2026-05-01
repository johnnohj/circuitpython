// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/main.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/main.c — Port initialization and frame loop.
//
// This is the heart of the wasm layer.  It provides:
//   port_init()  — one-time initialization
//   port_frame() — per-frame entry point with abort-resume landing
//
// The frame loop orchestrates each frame:
//   1. Start budget clock
//   2. Drain event ring
//   3. Step HAL (scan dirty flags, latch inputs)
//   4. Run lifecycle state machine (port_step)
//   5. If VM is active and aborts (budget or WFE), land here
//   6. Update state for JS, return result code
//
// On real boards, the equivalent is the main() while(1) loop that
// calls supervisor_run().  For us, each iteration of that loop is
// one call to port_frame() — JS drives the loop via rAF/setTimeout.
//
// Design refs:
//   design/wasm-layer.md                        (wasm layer, frame model)
//   design/behavior/04-script-execution.md      (lifecycle phases)
//   design/behavior/06-runtime-environments.md  (frame budget, abort-resume)
//   design/behavior/08-acceptance-criteria.md   (idle until user action)

#include <stdint.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mphal.h"
#include "port/port_memory.h"
#include "port/memfs_imports.h"
#include "port/ffi_imports.h"
#include "port/hal.h"
#include "port/serial.h"
#include "port/budget.h"
#include "port/port_step.h"
#include "port/event_ring.h"
#include "port/constants.h"
#include "supervisor/background_callback.h"

// ── port_init ──
//
// One-time initialization.  Called once after WASM instantiation.
// Sets up port memory, registers MEMFS regions, initializes HAL.
//
// On real boards, this is port_init() in supervisor/port.c, called
// from main.c before mp_init().  We do the same: set up the hardware
// substrate before the VM exists.

void port_init(void) {
    // Zero all port memory
    memset(&port_mem, 0, sizeof(port_mem));

    // Restore non-zero defaults
    port_mem.active_ctx_id = -1;

    // Register MEMFS regions (makes port_mem visible to JS)
    port_memory_register();

    // Initialize HAL (zero GPIO/analog, clear dirty, init categories)
    hal_init();

    // Enter PHASE_INIT — the first chassis_frame() call will run
    // step_init() which calls do_start_mp() (mp_init, GC, VFS, etc.)
    // and then transitions to PHASE_IDLE.
    port_mem.state.phase = PHASE_INIT;
    port_mem.state.flags = PF_INITIALIZED;
}

// ── port_frame ──
//
// Per-frame entry point.  JS calls this from rAF or setTimeout.
//
// Arguments:
//   now_us    — current timestamp in microseconds (from performance.now())
//   budget_us — frame budget in microseconds (0 = use default)
//
// Returns RC_DONE, RC_YIELD, RC_EVENTS, or RC_ERROR.
//
// The abort-resume landing: when the VM is active and triggers
// mp_sched_vm_abort() (from budget check, WFE, or port_idle_until_interrupt),
// nlr_jump_abort fires and lands at the nlr_set_abort point below.
// The C stack is destroyed, but code_state is on pystack (in port_mem),
// so resume is safe on the next frame.
//
// When MICROPY_ENABLE_VM_ABORT is set, nlr_set_abort wraps port_step()
// so abort-resume lands back here with RC_YIELD.

uint32_t port_frame(uint32_t now_us, uint32_t budget_us) {
    port_state_t *st = port_state();

    // Record frame metadata
    st->now_us = now_us;
    st->budget_us = budget_us;
    st->frame_count++;
    st->flags &= ~(PF_HAS_EVENTS | PF_HAL_DIRTY);

    // Start budget clock.  Soft (8ms) and firm (10ms) deadlines are
    // design constants — not per-frame parameters.  The budget_us arg
    // from JS is ignored; the contract is fixed.
    (void)budget_us;
    budget_frame_start();

    // Phase 1: Drain JS→C events
    if (event_ring_pending() > 0) {
        event_ring_drain();
        st->flags |= PF_HAS_EVENTS;
    }

    // Phase 2: HAL step — scan dirty flags, latch inputs
    hal_step();

    // Phase 3: Lifecycle state machine
    // nlr_set_abort is the abort-resume landing point.  When the VM
    // triggers mp_sched_vm_abort() (from budget check, WFE, or
    // port_idle_until_interrupt), nlr_jump_abort fires and lands here.
    // The C stack is destroyed, but code_state is on pystack (in
    // port_mem), so resume is safe on the next frame.
    uint32_t rc;
    #if MICROPY_ENABLE_VM_ABORT
    // Set the abort landing point.  nlr_jump_abort() (triggered by
    // mp_sched_vm_abort in vm_abort.c) will jump to nlr_top, which
    // nlr_push sets.  nlr_set_abort just records which buf to use.
    nlr_buf_t nlr_abort_buf;
    nlr_set_abort(&nlr_abort_buf);
    if (nlr_push(&nlr_abort_buf) == 0) {
        rc = port_step();
        nlr_pop();
    } else {
        // Landed here via nlr_jump_abort (budget, WFE, or idle)
        rc = RC_YIELD;
    }
    #else
    rc = port_step();
    #endif

    // Phase 4: Background tasks (displayio refresh, tick callbacks)
    background_callback_run_all();

    // Phase 4b: Update cursor info for JS-side rendering.
    // Must run after displayio refresh (Phase 4) so cursor position
    // reflects any terminal scrolling that just happened.
    #if CIRCUITPY_DISPLAYIO
    extern void wasm_cursor_info_update(void);
    wasm_cursor_info_update();
    #endif

    // Phase 5: Compute packed frame result.
    //
    // Matches ports/wasm convention: result = (port | sup<<8 | vm<<16).
    // JS unpacks this to make scheduling and optimization decisions:
    //   - Skip display.paint() when port layer reports no HW changes
    //   - Skip renderBoardState() when port is quiet
    //   - Throttle frames when VM is sleeping
    //   - Detect code completion (CTX_DONE) for UI transitions
    //
    // Port layer result codes (bits 0-7):
    #define WASM_PORT_QUIET         0
    #define WASM_PORT_EVENTS        1
    #define WASM_PORT_BG_PENDING    2
    #define WASM_PORT_HW_CHANGED    3
    // Supervisor layer (bits 8-15):
    #define WASM_SUP_IDLE           0
    #define WASM_SUP_SCHEDULED      1
    #define WASM_SUP_CTX_DONE       2
    #define WASM_SUP_ALL_SLEEPING   3
    // VM layer (bits 16-23):
    #define WASM_VM_NOT_RUN         0
    #define WASM_VM_YIELDED         1
    #define WASM_VM_SLEEPING        2
    #define WASM_VM_COMPLETED       3
    #define WASM_VM_EXCEPTION       4

    st->elapsed_us = budget_elapsed_us();

    // Port result
    uint8_t port_result = WASM_PORT_QUIET;
    if (st->flags & PF_HAS_EVENTS)       port_result = WASM_PORT_EVENTS;
    if (st->flags & PF_HAL_DIRTY)        port_result = WASM_PORT_HW_CHANGED;
    if (background_callback_pending())   port_result = WASM_PORT_BG_PENDING;

    // Supervisor result
    uint8_t sup_result = WASM_SUP_IDLE;
    if (st->phase == PHASE_CODE || st->phase == PHASE_BOOT) {
        sup_result = (rc == RC_DONE && st->phase == PHASE_IDLE)
            ? WASM_SUP_CTX_DONE
            : WASM_SUP_SCHEDULED;
    } else if (st->phase == PHASE_REPL) {
        sup_result = WASM_SUP_SCHEDULED;
    }

    // VM result
    uint8_t vm_result = WASM_VM_NOT_RUN;
    if (st->phase == PHASE_CODE || st->phase == PHASE_REPL) {
        if (rc == RC_YIELD) {
            // Abort landed — check why
            if (port_mem.vm_abort_reason == VM_ABORT_WFE) {
                vm_result = WASM_VM_SLEEPING;
            } else {
                vm_result = WASM_VM_YIELDED;  // budget expired
            }
        } else if (rc == RC_DONE) {
            vm_result = WASM_VM_COMPLETED;
        } else if (rc == RC_ERROR) {
            vm_result = WASM_VM_EXCEPTION;
        }
    }
    // After step_code completes, phase transitions to IDLE —
    // detect this as CTX_DONE for JS.
    if (sup_result == WASM_SUP_SCHEDULED && st->phase == PHASE_IDLE) {
        sup_result = WASM_SUP_CTX_DONE;
    }

    uint32_t result = port_result | (sup_result << 8) | (vm_result << 16);
    st->status = result;

    // Schedule next frame.  The design is a steady 60fps loop:
    // C takes what it needs (up to 8ms soft), JS fills the rest
    // of the ~16ms frame.  Always request ASAP (hint=0) unless
    // fully idle — JS maintains the 60fps cadence via rAF.
    ffi_request_frame(st->phase == PHASE_IDLE ? 0xFFFFFFFF : 0);

    return result;
}
