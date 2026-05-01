# JS as Coprocessor

**Created**: 2026-04-28
**Status**: Conceptual lens — informs design, not an implementation plan yet

## The Analogy

On real dual-core CircuitPython boards (ESP32-S3, RP2350), the second
core handles background work — WiFi stack, BLE, display refresh —
while the main core runs Python.  The cores communicate through shared
SRAM with hardware semaphores.

Our architecture has the same shape:

| Real dual-core | Our model |
|---|---|
| Core 0: Python VM | WASM: Python VM (bytecode interpreter) |
| Core 1: WiFi/BLE/display | JS: DOM events, canvas, WebUSB, WebSocket |
| Shared SRAM | MEMFS in linear memory (port_mem) |
| Hardware semaphores | Dirty flags + event ring |
| DMA transfers | FFI synchronous imports |
| Interrupt controller | DOM event system (pin listeners) |
| Timer peripheral | performance.now() / rAF / setTimeout |
| UART peripheral | Serial ring buffers (port_mem.serial_rx/tx) |

## DMA-like FFI

The synchronous WASM imports (`port.getMonotonicMs()`,
`port.getCpuTemperature()`, `port.getCpuVoltage()`) are structurally
identical to DMA register reads:

1. The VM issues a read (WASM import call)
2. The "peripheral" (JS) returns a value from its own state
3. The VM continues — no interrupt, no context switch, no buffering
4. The data just appears, as if memory-mapped

Like DMA, the "peripheral" (JS) updates its state independently of
the "CPU" (VM).  JS handles DOM events, processes user input, manages
WebSocket connections, renders canvas — all while the VM is between
frames or yielded via abort-resume.

## The DOM as Interrupt Controller

On real hardware, GPIO interrupts fire when a pin changes state.
The interrupt handler saves the value and sets a flag.  The main
loop checks the flag and processes the change.

Our model:
1. User clicks a button on the board SVG (DOM event)
2. JS pin listener fires (registered via `port.registerPinListener`)
3. JS writes the new value to the MEMFS GPIO slot
4. JS pushes an event into the event ring
5. Next frame, C drains the event ring and reads the latched value

The DOM event system IS the interrupt controller.  Pin listeners
ARE interrupt handlers.  The event ring IS the pending interrupt
queue.  The dirty flags ARE the interrupt status register.

## Coprocessor Capabilities

If we think of JS as a coprocessor, its "peripherals" include:

- **Timer**: `performance.now()`, `setTimeout`, `rAF`
- **Display**: Canvas 2D/WebGL, OffscreenCanvas in Worker
- **Storage**: IndexedDB (async persistence), OPFS
- **Network**: `fetch`, WebSocket, WebRTC
- **USB/Serial**: WebUSB, WebSerial (for external hardware)
- **Audio**: Web Audio API
- **Input**: Keyboard, mouse, touch, gamepad
- **Sensors**: Ambient light, accelerometer (device APIs)
- **Crypto**: SubtleCrypto

Each of these could be exposed to Python as a "peripheral" on the
coprocessor, accessed through jsffi or through common-hal modules
that proxy to JS.

## jsffi as Coprocessor Interface

Under this lens, the jsffi module isn't an "escape hatch to JS" —
it's the coprocessor communication interface:

```python
import jsffi

# Read a coprocessor register (synchronous DMA-like)
temp = jsffi.global_this.navigator.hardwareConcurrency

# Invoke a coprocessor function (like a coprocessor command)
response = jsffi.global_this.fetch("/api/data")

# Create a proxy object (like a coprocessor peripheral handle)
canvas = jsffi.global_this.document.getElementById("display")
```

The JsProxy objects are peripheral handles.  Attribute access is
register read.  Method calls are commands.  The PVN marshaling
layer is the coprocessor data bus.

## What This Means for Design

1. **The packet protocol is the inter-processor communication (IPC)
   format.**  Per-frame packets are like the shared-memory mailbox
   that dual-core MCUs use for core-to-core messaging.

2. **MEMFS is shared SRAM.**  Both "cores" can read and write it.
   Dirty flags provide the coherency protocol (like cache line
   invalidation).

3. **The frame loop is the main core's event loop.**  On a real
   MCU, Core 0 runs `while(1) { process_events(); run_vm(); }`.
   We do the same: `chassis_frame() { drain_events(); hal_step();
   port_step(); }`.

4. **abort-resume is the preemption mechanism.**  On real dual-core,
   Core 1 can interrupt Core 0 via an inter-processor interrupt.
   For us, `mp_sched_vm_abort()` is the IPI — it tells the VM to
   stop and return control to the frame loop.

5. **`port_idle_until_interrupt` is WFI (Wait For Interrupt).**
   The VM has nothing to do, so it sleeps until the coprocessor
   (JS) has something new.  On a real MCU, WFI gates the clock
   until an interrupt fires.  For us, it aborts back to JS and
   JS schedules the next frame when there's work.

## Future Possibilities

- **`microcontroller.Processor`** could expose JS as a second
  processor with its own capabilities (navigator.hardwareConcurrency,
  deviceMemory, etc.)

- **Supervisor multi-core primitives** (`supervisor.Runtime`) could
  describe the JS coprocessor's state (busy, idle, connected)

- **Common-hal modules for web APIs** could present WebSocket as
  a UART-like peripheral, fetch as a file-like object, Web Audio
  as audioio — all accessed through the "coprocessor" model

- **External hardware via WebUSB/WebSerial** becomes a third
  processor — the real MCU connected to the browser, with the
  browser (JS coprocessor) acting as a bridge

## Not an Implementation Plan

This is a conceptual lens, not a refactoring proposal.  The existing
code already works this way — this document names the pattern so we
can reason about it consistently and make design decisions that
strengthen rather than accidentally break the analogy.

The key insight: **we didn't invent a new architecture.  We're
implementing a standard dual-core MCU architecture where one of
the cores happens to be a web browser.**
