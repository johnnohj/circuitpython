# The WASM Layer

**Created**: 2026-04-28
**Context**: Phase 0.1 review of mphalport.h revealed that `wasi_mphal.c`
and `hal.c` are two faces of the same conceptual layer.  Further
discussion confirmed that the port-specific `supervisor/`, `chassis/`,
and `port_memory` all belong to this same layer.

## Insight

On a real RP2040, `mp_hal_delay_ms`, `reset_port`,
`common_hal_digitalio_digitalinout_set_value`, and the SysTick handler
all ultimately talk to the same thing: hardware registers on the chip.
Nobody thinks of them as separate layers — they're all "talking to the
chip."  The split is about which *contract* they fulfill, not about
what substrate they sit on.

Same for us.  Every port-specific C file talks to the same substrate:
WASM linear memory, WASI syscalls, and JS FFI.  Whether it fulfills
MicroPython's `mp_hal_*` contract, CircuitPython's `port_*` lifecycle
contract, CircuitPython's `common-hal` peripheral contract, or our own
frame-loop/abort-resume machinery — it's all one layer adapting upstream
contracts to our platform.

## The nesting model

```
JS runtime (host)
  └─ wasm layer (ALL port-specific C code: the simulation contract)
      └─ CircuitPython (supervisor/shared/, shared-bindings/, shared-module/)
          └─ MicroPython VM (py/)
              └─ Python (code.py)
```

Each layer insulates the one inside it from the one outside it:
- Python doesn't know about the VM
- The VM doesn't know about CircuitPython
- CircuitPython doesn't know about WASM
- The wasm layer presents itself to CircuitPython exactly as a real
  chip would

The boundary between CircuitPython and the wasm layer is defined by
the contracts in:
- `supervisor/port.h` — lifecycle (port_init, reset_port, etc.)
- `py/mphal.h` + `mphalport.h` — platform services (timing, I/O)
- `common-hal/` function signatures — peripheral access
- `supervisor/shared/*.h` — background tasks, serial, tick

Everything above those contracts is portable CircuitPython.
Everything below them is our wasm layer.

## What belongs in the wasm layer

The wasm layer is ALL port-specific code: everything that fulfills a
contract demanded by the layers above using the substrate below.

| File | Contract | Substrate |
|------|----------|-----------|
| `wasi_mphal.c` | `mp_hal_*` (MP platform) | WASI syscalls, JS FFI |
| `hal.c` | peripheral access (common-hal) | MEMFS, JS FFI |
| `supervisor/port.c` | `port_*` (CP lifecycle) | WASM memory, JS FFI |
| `supervisor/serial.c` | `serial_*` (CP console) | ring buffers in port_mem |
| `supervisor/tick.c` | `port_background_tick` | frame budget, event ring |
| `main.c` (frame loop) | frame scheduling | rAF, abort-resume |
| `port_memory.c` | frame-durable state | WASM linear memory |
| `budget.c` | cooperative yield | WASI clock, frame deadline |
| `vm_abort.c` | VM halt/resume | nlr_jump, code_state |
| `event_ring.c` | JS→C event delivery | MEMFS ring buffer |
| `serial.c` | C→JS/JS→C console | ring buffers in port_mem |
| `compile.c` | code compilation | MP compiler API |
| `ffi_exports.c` | JS→C entry points | WASM exports |
| `common-hal/*` | CP module backends | hal.c, MEMFS |

All of these are "talking to the chip" — our chip just happens to be
WASM + JS.

## Why our port-specific supervisor/ is wasm layer, not CP layer

The upstream `supervisor/shared/` is CircuitPython proper — portable
code all ports share.  It defines contracts (`port.h`, `tick.h`,
`serial.h`).

Our `ports/wasm/supervisor/` implements those contracts against our
substrate.  It's not a layer above the chassis — it's the same layer
wearing a different hat.  `supervisor/port.c` and `chassis/main.c`
both adapt upstream expectations to WASM+JS; one fulfills the `port_*`
contract and the other drives the frame loop.

On an RP2040, the equivalent files would be `ports/raspberrypi/supervisor/port.c`
(fulfilling `port_*`) and the `main()` in the RP2040 SDK startup code
(driving the main loop).  Both talk to the chip.  Same for us.

## Implication for directory structure

All wasm-layer files live in `port/` (the directory).  The PLAN phases
still build in dependency order:

1. **Phase 1** — primitives: memory, budget, serial, hal, abort-resume,
   frame loop (no upstream contract dependencies)
2. **Phase 3** — platform HAL: wasi_mphal.c, mpthreadport, gccollect
   (fulfills `mp_hal_*` and `py/` contracts)
3. **Phase 4** — supervisor adaptation: port.c, tick.c, serial.c,
   compile.c (fulfills `supervisor/port.h` contracts using Phase 1
   primitives)
4. **Phase 5** — common-hal: digitalio, analogio, busio, etc.
   (fulfills `common-hal/` contracts using hal.c)

The phase split is build order, not a claim about separate layers.
They're all wasm layer.

## Decision: Option A — adopt upstream supervisor (2026-04-28)

Three options were considered:

- **Option A: Adopt upstream supervisor/shared/**. Implement only the
  `port_*` primitives it calls.  The supervisor orchestrates delays,
  background tasks, and serial routing.
- **Option B: Supersede upstream supervisor/shared/**. Own everything,
  don't link against upstream supervisor at all.
- **Option C: Current hybrid**. Use some upstream, override other parts.
  Source of the `mp_hal_delay_ms` gap (our override bypasses
  `RUN_BACKGROUND_TASKS`).

**Option A was chosen.**  Rationale:

1. The upstream `mp_hal_delay_ms` in `supervisor/shared/tick.c` already
   does exactly what we need: calls `RUN_BACKGROUND_TASKS`, checks
   `mp_hal_is_interrupted()`, and idles via `port_idle_until_interrupt()`.
   We just need to implement that last function as abort-resume.

2. On a real board, `port_idle_until_interrupt` puts the CPU to sleep
   and a timer interrupt wakes it.  For us, "sleep" = abort back to JS,
   "timer interrupt" = next frame callback re-enters C.  The mapping
   is direct and clean.

3. We get background task execution during delays for free.  No more
   overriding `mp_hal_delay_ms` and accidentally bypassing the
   supervisor's orchestration.

**Concrete changes required**:

- `mpconfigport.h`: remove `#define mp_hal_delay_ms mp_hal_delay_ms`
  override.  Let the supervisor's weak definition in tick.c take effect.
- `port_idle_until_interrupt()`: implement as abort-resume yield to JS.
- `port_get_raw_ticks()`: return WASI `clock_gettime` values converted
  to 1/1024-second ticks.
- `port_interrupt_after_ticks()`: store the deadline so the frame loop
  knows when to re-enter the VM.
- Evaluate `supervisor/shared/micropython.c` for console routing —
  can it work with our serial ring buffers, or do we still need a
  port-specific replacement?

**What we still own directly** (safe, no supervisor mediation):

- `mp_hal_ticks_ms()`, `mp_hal_ticks_us()` — pure time queries
- `mp_hal_delay_us()` — short delays, no background tasks needed
- `mp_hal_get_random()` — pure utility
- `mp_hal_set_interrupt_char()` — WASI has no signals, our impl is
  a no-op (interrupt comes via MEMFS rx buffer)

**What the upstream supervisor provides** (we must NOT override):

- `mp_hal_delay_ms()` — supervisor orchestrates delay + background
  tasks + interrupt checking; we provide the primitives
- `mp_hal_stdout_tx_strn()` / `mp_hal_stdin_rx_chr()` — supervisor
  routes through serial backends; we provide the ring buffers

**Not all of supervisor/shared/ applies**.  Files we likely still
replace or skip:

- USB-related (no USB on WASM)
- BLE-related (no BLE)
- safe_mode.c — may need adaptation (no hardware reset button)
- workflow.c — web workflow is different from our browser environment

The decision is: use upstream orchestration where it fits, replace
where it genuinely doesn't.  But never half-use it (Option C).

## The analogy

CircuitPython adds `supervisor/` and `common-hal/` as abstraction
layers so many types of microcontrollers can run standard modules —
`CircuitPython digitalio` instead of `digitalio for RP2040`.

We do the same thing one level out.  Our wasm layer wraps and
insulates the CircuitPython+MicroPython substrate the way that
substrate wraps and insulates Python execution from host runtimes.
`hal.c` bridges between what `common-hal/` expects and our JS-side
simulation, because although we don't have real hardware, the driving
logic needs to come from somewhere.
