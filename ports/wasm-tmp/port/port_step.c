// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Inspired by ports/wasm/supervisor/supervisor.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/port_step.c — Port lifecycle state machine.
//
// Two execution models, selected by build flag:
//
//   MICROPY_VM_YIELD_ENABLED (Option 1):
//     Carried forward from ports/wasm.  The VM yields by returning
//     MP_VM_RETURN_SUSPEND — a clean C stack unwind.  Resume enters
//     at the innermost code_state via mp_vm_yield_state.  Requires
//     upstream py/vm.c modifications (already in our fork).
//     Handles imports, constructors, nested calls correctly.
//
//   MICROPY_ENABLE_VM_ABORT (Option 3):
//     abort-resume via nlr_jump_abort.  Budget enforcement is deferred
//     until the first HOOK_LOOP (backwards branch).  Setup/import code
//     runs to completion on the first frame.  Once the main loop starts,
//     ip/sp saved by HOOK_LOOP are always current.
//
// Both share: core_init(), cleanup_between_runs(), step_repl(), and
// the frame loop structure in main.c.  The VM is long-lived (Option B):
// initialized once, persists across code/REPL transitions.
//
// Design refs:
//   ports/wasm/supervisor/supervisor.c     (_core_init, wasm_frame, vm_yield_step)
//   ports/wasm/design/abort-resume.md      (halt/resume comparison)
//   design/behavior/04-script-execution.md (boot, code, REPL sequence)
//   design/behavior/05-vm-lifecycle.md     (cleanup_after_vm, start_mp)
//   design/behavior/06-runtime-environments.md (frame budget, abort-resume)

#include <stdbool.h>
#include <string.h>

#include "port/port_step.h"
#include "port/port_memory.h"
#include "port/hal.h"
#include "port/constants.h"

#include "py/runtime.h"
#include "py/gc.h"
#include "py/bc.h"
#include "py/mphal.h"
#include "py/cstack.h"
#include "supervisor/compile.h"
#include "supervisor/shared/serial.h"
#include "shared/runtime/pyexec.h"
#include "common-hal/microcontroller/Pin.h"

#if CIRCUITPY_DISPLAYIO
#include "board_display.h"
#endif

#if CIRCUITPY_STATUS_BAR
#include "supervisor/shared/status_bar.h"
#endif

#if MICROPY_ENABLE_PYSTACK
#include "py/pystack.h"
#endif

#if MICROPY_VFS_POSIX
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#endif

// ── vm_yield forward declarations (Option 1) ──

#if MICROPY_VM_YIELD_ENABLED
extern void vm_yield_set_frame_start(void);
extern void vm_yield_start(mp_code_state_t *cs);
extern int  vm_yield_step(void);
extern void vm_yield_stop(void);
extern bool vm_yield_code_running(void);
extern int  vm_yield_delay_active(void);
#endif

// ── State ──

static bool _repl_initialized = false;

// Only used in Option 3 (abort-resume).  Option 1 uses vm_yield's state.
#if MICROPY_ENABLE_VM_ABORT
static mp_code_state_t *_code_state = NULL;
#endif

// ── Phase transitions ──

void port_step_start_boot(void) {
    port_mem.state.phase = PHASE_BOOT;
    port_mem.state.sub_phase = SUB_START_MP;
}

void port_step_start_code(void) {
    port_mem.state.phase = PHASE_CODE;
    port_mem.state.sub_phase = SUB_START_MP;
}

void port_step_start_repl(void) {
    port_mem.state.phase = PHASE_REPL;
    port_mem.state.sub_phase = 0;
    _repl_initialized = false;
}

void port_step_go_idle(void) {
    port_mem.state.phase = PHASE_IDLE;
    port_mem.state.sub_phase = 0;
}

void port_step_soft_reboot(void) {
    reset_all_pins();
    hal_release_all();
    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_stop();
    #endif
    #if MICROPY_ENABLE_VM_ABORT
    _code_state = NULL;
    #endif
    port_step_go_idle();
}

uint32_t port_step_phase(void) {
    return port_mem.state.phase;
}

// ── VM initialization (once) ──
//
// Mirrors _core_init() from ports/wasm/supervisor/supervisor.c.
// Called once from step_init on the first chassis_frame after port_init.
// The VM persists across code/REPL transitions (Option B).

static void core_init(void) {
    #if MICROPY_PY_THREAD
    mp_thread_init();
    #endif

    mp_uint_t stack_size = 40000 * (sizeof(void *) / 4);
    mp_cstack_init_with_sp_here(stack_size);

    gc_init(port_gc_heap(), port_gc_heap() + port_gc_heap_size());

    #if MICROPY_ENABLE_PYSTACK
    mp_pystack_init(port_pystack(0), port_pystack_end(0));
    #endif

    mp_init();

    #if CIRCUITPY_DISPLAYIO
    board_display_init();
    #endif

    #if CIRCUITPY_STATUS_BAR
    supervisor_status_bar_init();
    supervisor_status_bar_start();
    #endif

    #if MICROPY_VFS_POSIX
    {
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(
                &mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    #endif

    mp_sys_path = mp_obj_new_list(0, NULL);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    mp_obj_list_append(mp_sys_path,
        mp_obj_new_str_via_qstr(MICROPY_PY_SYS_PATH_DEFAULT,
            strlen(MICROPY_PY_SYS_PATH_DEFAULT)));
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);

    port_mem.vm_flags |= VM_FLAG_INITIALIZED;
}

// ── Cleanup between runs (Option B) ──
//
// Targeted reset: release hardware claims, clear pending state.
// VM stays alive — GC heap, module cache, VFS, display all persist.

static void cleanup_between_runs(void) {
    MP_STATE_THREAD(mp_pending_exception) = MP_OBJ_NULL;
    #if MICROPY_ENABLE_VM_ABORT
    MP_STATE_VM(vm_abort) = false;
    #endif
    reset_all_pins();
    hal_release_all();
}

// ── Frame dispatch ──

static uint32_t step_init(void) {
    if (!(port_mem.vm_flags & VM_FLAG_INITIALIZED)) {
        core_init();
    }
    port_step_go_idle();
    return RC_DONE;
}

static uint32_t step_idle(void) {
    return RC_DONE;
}

static uint32_t step_boot(void) {
    switch (port_mem.state.sub_phase) {
    case SUB_START_MP:
        port_mem.state.sub_phase = SUB_EXECUTE;
        return RC_YIELD;

    case SUB_EXECUTE: {
        pyexec_result_t result;
        pyexec_file_if_exists("boot.py", &result);
        port_step_go_idle();
        return RC_DONE;
    }

    default:
        port_step_go_idle();
        return RC_DONE;
    }
}

// ── step_code: Option 1 (vm_yield) ──

#if MICROPY_VM_YIELD_ENABLED

static uint32_t step_code(void) {
    switch (port_mem.state.sub_phase) {
    case SUB_START_MP:
        cleanup_between_runs();
        {
            char *path = port_input_buf();
            mp_code_state_t *cs = (path[0] != '\0')
                ? cp_compile_file(path)
                : NULL;
            if (!cs) {
                port_step_go_idle();
                return RC_DONE;
            }
            vm_yield_start(cs);
        }
        port_mem.state.sub_phase = SUB_EXECUTE;
        return RC_YIELD;

    case SUB_EXECUTE: {
        vm_yield_set_frame_start();
        int ret = vm_yield_step();
        if (ret == 0 || ret == 2) {
            // Completed or exception
            port_mem.state.sub_phase = SUB_CLEANUP;
            return RC_YIELD;
        }
        // Yielded — call again next frame
        return RC_YIELD;
    }

    case SUB_CLEANUP:
        port_step_go_idle();
        return RC_DONE;

    default:
        port_step_go_idle();
        return RC_DONE;
    }
}

#endif // MICROPY_VM_YIELD_ENABLED

// ── step_code: Option 3 (abort-resume) ──

#if MICROPY_ENABLE_VM_ABORT

static uint32_t step_code(void) {
    switch (port_mem.state.sub_phase) {
    case SUB_START_MP:
        // Targeted cleanup + compile OUTSIDE the abort boundary.
        // Compilation is synchronous C code that runs to completion.
        // Only bytecode execution (SUB_EXECUTE) is abort-resumable.
        cleanup_between_runs();
        {
            char *path = port_input_buf();
            _code_state = (path[0] != '\0')
                ? cp_compile_file(path)
                : NULL;
        }
        if (!_code_state) {
            port_step_go_idle();
            return RC_DONE;
        }
        port_mem.state.sub_phase = SUB_EXECUTE;
        return RC_YIELD;

    case SUB_EXECUTE:
        // If sleeping (WFE abort set a deadline), skip the VM until
        // the deadline passes.  HAL and background callbacks still run.
        if (port_mem.wakeup_ms > 0 &&
            mp_hal_ticks_ms() < port_mem.wakeup_ms) {
            return RC_YIELD;
        }
        port_mem.wakeup_ms = 0;

        // Execute compiled bytecode.  Runs inside port_frame's
        // nlr_set_abort boundary.  If the VM aborts (budget expired),
        // code_state->ip/sp were saved by HOOK_LOOP, and next frame
        // we re-enter here to resume via mp_execute_bytecode.
        if (_code_state) {
            mp_execute_bytecode(_code_state, MP_OBJ_NULL);
            _code_state = NULL;
        }
        port_mem.state.sub_phase = SUB_CLEANUP;
        return RC_YIELD;

    case SUB_CLEANUP:
        _code_state = NULL;
        port_step_go_idle();
        return RC_DONE;

    default:
        port_step_go_idle();
        return RC_DONE;
    }
}

#endif // MICROPY_ENABLE_VM_ABORT

// ── step_repl (shared by both options) ──

static uint32_t step_repl(void) {
    if (!_repl_initialized) {
        cleanup_between_runs();
        pyexec_event_repl_init();
        _repl_initialized = true;
    }

    #if MICROPY_VM_YIELD_ENABLED
    vm_yield_set_frame_start();
    #endif

    while (serial_bytes_available()) {
        int c = serial_read();
        int ret = pyexec_event_repl_process_char(c);
        if (ret & PYEXEC_FORCED_EXIT) {
            _repl_initialized = false;
            port_step_soft_reboot();
            return RC_DONE;
        }
    }

    return RC_DONE;
}

// ── Main dispatch ──

uint32_t port_step(void) {
    switch (port_mem.state.phase) {
    case PHASE_INIT:  return step_init();
    case PHASE_IDLE:  return step_idle();
    case PHASE_BOOT:  return step_boot();
    case PHASE_CODE:  return step_code();
    case PHASE_REPL:  return step_repl();
    default:          return RC_ERROR;
    }
}
