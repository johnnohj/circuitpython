# Deviation Summary

Back to [README](README.md)

Every deviation from upstream CircuitPython behavior, categorized.

---

## Structural (must deviate -- C stack doesn't persist on WASM)

| Area | Upstream | Our port |
|------|----------|----------|
| Execution model | Blocking (C stack persists) | Frame-budgeted abort-resume |
| Sleep/WFI | C stack paused (OS preserves it) | Abort -> return to JS -> resume next frame |
| Memory ownership | Stack + malloc + GC | All state in port_mem (static linear memory) |
| VM stepping | Single call, runs to completion | Many calls, ~8-10ms each |

These are inherent to the WASM platform.  They cannot be eliminated,
only managed well.

---

## Environmental (runtime doesn't offer the primitive)

| Area | Upstream | Our port |
|------|----------|----------|
| Hardware | Real GPIO, SPI, I2C, UART, USB | Simulated via MEMFS + event ring |
| Safe mode button | Physical button during boot | Gated behind ENABLE_SIM_SAFE_MODE (future) |
| Filesystem | FatFS on flash | POSIX VFS over WASI |
| Serial | UART or USB CDC | Ring buffer + environment-specific transport |
| ISR | Hardware timer (SysTick) | Budget check in HOOK_LOOP |

These could theoretically be bridged (e.g., WebUSB for real hardware)
but the simulated versions serve the primary use case.

---

## Intentional (we choose to deviate for UX or capability)

| Area | Upstream | Our port | Rationale |
|------|----------|----------|-----------|
| REPL input | Character-at-a-time via UART | Keystroke buffer in frame packet -> C readline | JS captures keys, C does line editing/completion |
| Boot sequence | Auto-run code.py | Idle until user action (Run/Save) | Better UX for interactive development |
| Board definition | C pin tables | JSON + SVG | Enables visual board, runtime definition, user-provided pinouts |
| Context system | Single VM context | Multi-context scheduler | Retained (does no harm), lower-level primitive |
| Pin categories | Implicit in board.c | Explicit in definition.json | Self-describing for UI |
| Pin pull up/down | C (common-hal) | JS (toggle event) | Simpler UI interaction model |

These are features, not compromises.  They should be preserved.

### REPL input model

The previous design had JS owning readline entirely (line editing,
history, completion).  The new model feeds keystrokes to C-side
readline as a background task (part of the frame packet).  This keeps
us closer to upstream readline behavior and enables tab completion
and hints through the standard C path, while JS still owns keyboard
capture and display rendering.

---

## Gaps (should match upstream but don't yet)

| Area | What's missing | Priority | Notes |
|------|---------------|----------|-------|
| boot.py lifecycle | Not full start_mp/cleanup_after_vm cycle | High | Abort-resume enables this |
| cleanup_after_vm ordering | May not match upstream sequence | High | Audit during Phase 4 |
| stack_ok() | Always returns true | Medium | Should check against 16K limit |
| Safe mode | No way to trigger | Medium | ENABLE_SIM_SAFE_MODE flag (future) |
| Default file creation | No code.py created on first mount | Medium | JS or C init |
| settings.toml | Not read for heap/pystack sizing | Low | Support with preset options |
| mp_thread_init ordering | Needs audit | High | Must be before mp_init for asyncio |
| port_enable_tick / port_disable_tick | No-op | High | Should control background task scheduling |
| mp_sched_schedule | Not tested | Medium | Event drain phase = interrupt context |
| Watchdog | None | Low | Worker/child process can be killed; main thread runaway = user intervention |

Gaps are the backlog.  Each one represents a place where user code
might behave differently on our port vs a real board.  High-priority
gaps should be closed during migration; medium and low can be tracked
for later.
