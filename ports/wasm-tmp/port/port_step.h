// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Inspired by ports/wasm/chassis/port_step.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/port_step.h — Port lifecycle state machine.
//
// Tracks the high-level lifecycle phase (idle, boot.py, code.py, REPL)
// and provides the per-frame entry point.  The actual VM execution is
// managed by the supervisor (Phase 4); this tracks where we are in the
// CircuitPython lifecycle so the frame function knows what to do.
//
// Design refs:
//   design/behavior/04-script-execution.md  (boot, code, REPL sequence)
//   design/behavior/05-vm-lifecycle.md      (start_mp, cleanup_after_vm)
//   design/behavior/08-acceptance-criteria.md (idle-until-user-action)
//   design/wasm-layer.md                    (Option A, frame model)

#ifndef PORT_STEP_H
#define PORT_STEP_H

#include <stdint.h>

// ── Lifecycle phases ──
// These track where we are in the CircuitPython lifecycle.
// Stored in port_mem.state.phase.

#define PHASE_UNINIT     0  // port_memory not initialized
#define PHASE_INIT       1  // initializing (port_init, hal_init, mp_init)
#define PHASE_IDLE       2  // ready, waiting for user action
#define PHASE_BOOT       3  // running boot.py
#define PHASE_CODE       4  // running code.py
#define PHASE_REPL       5  // running REPL
#define PHASE_SAFE_MODE  6  // safe mode (future)
#define PHASE_SHUTDOWN   7  // shutting down

// ── Sub-phases ──
// Within a lifecycle phase, track finer-grained state.
// Stored in port_mem.state.sub_phase.

// Sub-phases for PHASE_BOOT / PHASE_CODE:
#define SUB_START_MP     0  // call start_mp (mp_init + pystack setup)
#define SUB_COMPILE      1  // compile the script
#define SUB_EXECUTE      2  // VM executing (abort-resume loop)
#define SUB_CLEANUP      3  // cleanup_after_vm

// Sub-phases for PHASE_REPL:
#define SUB_REPL_PROMPT  0  // waiting for input
#define SUB_REPL_COMPILE 1  // compiling input line
#define SUB_REPL_EXECUTE 2  // executing compiled line
#define SUB_REPL_RESULT  3  // displaying result

// ── Frame entry point ──

// Run one frame of the lifecycle state machine.
// Returns RC_DONE, RC_YIELD, RC_EVENTS, or RC_ERROR.
//
// Called from the frame function (port/main.c, Phase 1.10).
// Internally dispatches based on current phase:
//   IDLE:  drain events, check for work requests, return DONE
//   BOOT/CODE/REPL: run supervisor lifecycle step, return YIELD if VM active
//   SHUTDOWN: finalize, return DONE
uint32_t port_step(void);

// ── Phase transitions ──
// Called by the supervisor or JS exports to change lifecycle phase.

// Transition to PHASE_BOOT (called once at startup if boot.py exists)
void port_step_start_boot(void);

// Transition to PHASE_CODE (called by Run button or auto-reload)
void port_step_start_code(void);

// Transition to PHASE_REPL (called when entering REPL)
void port_step_start_repl(void);

// Transition to PHASE_IDLE (called after script completes or Ctrl-D)
void port_step_go_idle(void);

// Soft reboot: cleanup current phase, re-enter PHASE_INIT
void port_step_soft_reboot(void);

// Get current phase
uint32_t port_step_phase(void);

#endif // PORT_STEP_H
