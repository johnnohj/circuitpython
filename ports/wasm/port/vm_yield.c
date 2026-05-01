// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/vm_yield.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/vm_yield.c — Wall-clock frame budget + VM yield protocol.
//
// Adapted from ports/wasm/supervisor/vm_yield.c for single-context use.
// Multi-context scheduling and semihosting trace removed.
//
// The yield protocol:
//   1. HOOK_LOOP calls wasm_vm_hook_loop() at every backwards branch
//   2. wasm_vm_hook_loop() checks wall-clock budget
//   3. If budget expired, mp_vm_request_yield(YIELD_BUDGET)
//   4. vm.c's MICROPY_VM_YIELD_ENABLED block checks mp_vm_should_yield()
//   5. VM saves ip/sp/exc_sp to the innermost code_state
//   6. MICROPY_VM_YIELD_SAVE_STATE saves the code_state pointer
//   7. VM returns MP_VM_RETURN_SUSPEND — clean C stack unwind
//   8. Next frame: vm_yield_step() resumes at saved code_state
//
// This is ONLY compiled when MICROPY_VM_YIELD_ENABLED is set.
// When MICROPY_ENABLE_VM_ABORT is set instead, port/vm_abort.c provides
// the hook and yield functions.
//
// Design refs:
//   ports/wasm/supervisor/vm_yield.c      (original, multi-context)
//   ports/wasm/design/abort-resume.md     (comparison of yield vs abort)
//   design/behavior/06-runtime-environments.md  (frame budget model)

#include "py/mpconfig.h"
#if MICROPY_VM_YIELD_ENABLED

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "py/bc.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/objexcept.h"

#include "port/port_memory.h"
#include "port/budget.h"
#include "supervisor/shared/serial.h"
#include "supervisor/port.h"

#if MICROPY_ENABLE_PYSTACK
#include "py/pystack.h"
#endif

// ── Suspend sentinel ──
//
// When a C-called Python frame (fun_bc_call, gen_resume_and_raise)
// observes MP_VM_RETURN_SUSPEND, it nlr_raises this static sentinel
// to unwind the C stack.  vm_yield_step identity-checks the sentinel
// in its EXCEPTION handler and treats it as suspension rather than
// a genuine SystemExit.

mp_obj_exception_t mp_vm_suspend_sentinel = {
    .base = { &mp_type_SystemExit },
    .args = (mp_obj_tuple_t *)&mp_const_empty_tuple_obj,
    .traceback = NULL,
};

// ── Yield reasons ──

enum {
    YIELD_BUDGET  = 0,
    YIELD_SLEEP   = 1,
    YIELD_SHOW    = 2,
    YIELD_IO_WAIT = 3,
    YIELD_STDIN   = 4,
};

volatile int mp_vm_yield_reason = YIELD_BUDGET;
volatile uint32_t mp_vm_yield_arg = 0;

// Written by the VM yield handler (MICROPY_VM_YIELD_SAVE_STATE).
void *mp_vm_yield_state = NULL;

// ── Wall-clock budget ──

static uint64_t _frame_start_ms = 0;
static volatile bool _yield_requested = false;

static uint64_t _wall_clock_ms(void) {
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_nsec / 1000000;
}

void vm_yield_set_frame_start(void) {
    _frame_start_ms = _wall_clock_ms();
    _yield_requested = false;
    mp_vm_yield_reason = YIELD_BUDGET;
}

int mp_vm_should_yield(void) {
    return _yield_requested ? 1 : 0;
}

void mp_vm_request_yield(int reason, uint32_t arg) {
    mp_vm_yield_reason = reason;
    mp_vm_yield_arg = arg;
    _yield_requested = true;
}

// ── VM hook — runs at every backwards branch ──

extern void serial_check_interrupt(void);

void wasm_vm_hook_loop(const void *code_state_ptr) {
    (void)code_state_ptr;

    // Ctrl-C check
    serial_check_interrupt();

    // Lightweight tick — time-gated to ~1ms
    {
        static uint64_t last_tick = 0;
        uint64_t now = port_get_raw_ticks(NULL);
        if (now != last_tick) {
            last_tick = now;
            extern void supervisor_tick(void);
            supervisor_tick();
        }
    }

    // Budget check (skip in CLI mode and atomic sections)
    if (!_yield_requested && !mp_thread_in_atomic_section()) {
        uint64_t now = _wall_clock_ms();
        if (now - _frame_start_ms >= BUDGET_SOFT_MS) {
            mp_vm_request_yield(YIELD_BUDGET, 0);
        }
    }
}

// ── Sleep-as-yield ──
//
// time.sleep() sets a deadline and yields.  The frame loop checks
// the deadline before re-entering the VM.

static uint64_t _sleep_deadline_ms = 0;

int vm_yield_delay_active(void) {
    if (_sleep_deadline_ms == 0) return 0;
    if ((uint64_t)mp_hal_ticks_ms() >= _sleep_deadline_ms) {
        _sleep_deadline_ms = 0;
        return 0;
    }
    return 1;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    if (ms == 0) return;
    _sleep_deadline_ms = (uint64_t)mp_hal_ticks_ms() + ms;
    port_mem.wakeup_ms = _sleep_deadline_ms;
    mp_vm_request_yield(YIELD_SLEEP, (uint32_t)ms);
}
#define mp_hal_delay_ms mp_hal_delay_ms

// ── Stepping state (single context) ──

static mp_code_state_t *_code_state = NULL;
static bool _started = false;
static bool _first_entry = false;

void vm_yield_start(mp_code_state_t *cs) {
    _code_state = cs;
    _started = true;
    _first_entry = true;
    _sleep_deadline_ms = 0;
    mp_vm_yield_state = NULL;
}

bool vm_yield_code_running(void) {
    return _started;
}

// Execute one burst of bytecodes.
//
// Returns:
//   0 — normal completion
//   1 — yielded (call again next frame)
//   2 — exception
int vm_yield_step(void) {
    if (!_started || _code_state == NULL) {
        return 0;
    }

    // If sleeping, don't enter the VM
    if (vm_yield_delay_active()) {
        return 1;
    }

    // Reset yield flag for this burst
    _yield_requested = false;
    mp_vm_yield_reason = YIELD_BUDGET;

    mp_code_state_t *entry_state;

    if (_first_entry) {
        _first_entry = false;
        mp_vm_yield_state = NULL;
        entry_state = _code_state;
    } else if (mp_vm_yield_state != NULL) {
        // Resume at the innermost frame that was active when VM yielded.
        entry_state = (mp_code_state_t *)mp_vm_yield_state;
        mp_vm_yield_state = NULL;
    } else {
        entry_state = _code_state;
    }

    mp_vm_return_kind_t ret = mp_execute_bytecode(entry_state, MP_OBJ_NULL);

    switch (ret) {
        case MP_VM_RETURN_NORMAL:
            mp_globals_set(_code_state->old_globals);
            _started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_code_state);
            #endif
            _code_state = NULL;
            return 0;

        case MP_VM_RETURN_YIELD:
        case MP_VM_RETURN_SUSPEND:
            return 1;

        case MP_VM_RETURN_EXCEPTION: {
            mp_obj_t exc = MP_OBJ_FROM_PTR(_code_state->state[0]);

            // Identity-check the suspend sentinel
            if (exc == MP_OBJ_FROM_PTR(&mp_vm_suspend_sentinel)) {
                return 1;
            }

            if (mp_obj_is_subclass_fast(
                    MP_OBJ_FROM_PTR(((mp_obj_base_t *)MP_OBJ_TO_PTR(exc))->type),
                    MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
                _started = false;
                #if MICROPY_ENABLE_PYSTACK
                mp_pystack_free(_code_state);
                #endif
                _code_state = NULL;
                return 0;
            }
            mp_obj_print_exception(&mp_stderr_print, exc);
            mp_globals_set(_code_state->old_globals);
            _started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_code_state);
            #endif
            _code_state = NULL;
            return 2;
        }

        default:
            return 2;
    }
}

void vm_yield_stop(void) {
    if (_started && _code_state != NULL) {
        #if MICROPY_ENABLE_PYSTACK
        mp_pystack_free(_code_state);
        #endif
    }
    _code_state = NULL;
    _started = false;
    _sleep_deadline_ms = 0;
    _yield_requested = false;
}

#endif // MICROPY_VM_YIELD_ENABLED
