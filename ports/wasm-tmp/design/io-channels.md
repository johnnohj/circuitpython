# IO Channel Architecture — Unified Bus Model

**Created**: 2026-04-29
**Updated**: 2026-04-29 — collapsed two-channel model to unified bus
**Status**: Draft
**References**: [packet-protocol.md](packet-protocol.md),
[workflow-analog.md](workflow-analog.md),
[js-coprocessor.md](js-coprocessor.md),
[behavior/02-serial-and-stack.md](behavior/02-serial-and-stack.md)

## The USB Analogy

On real boards, USB is one physical wire carrying multiple logical
streams: CDC serial, MSC storage, HID, vendor endpoints.  The host
polls at regular intervals.  The device responds with data from
whichever endpoints have something to say.

Our analog:

| USB concept | Our analog |
|---|---|
| Physical wire | WASM linear memory (`port_mem`) |
| Host poll interval | `chassis_frame()` call |
| Control transfers (endpoint 0) | WASM exports (`cp_start_repl`, `cp_ctrl_c`, ...) |
| Bulk/interrupt endpoints | port_mem rings + MEMFS slots + dirty flags |
| Device descriptors | `definition.json` |
| Endpoint descriptors | port_mem layout (serial rings, GPIO slots, etc.) |

One bus.  One poll.  Multiple logical streams multiplexed through
shared memory.

## Design: port_mem IS the Bus

All data flows through `port_mem` in WASM linear memory.  Each
frame, JS writes inbound data before calling `chassis_frame()`
and reads outbound data after it returns.  The frame boundary is
the synchronization point — like a USB poll interval.

### Inbound (JS -> C, before frame)

| Stream | Location | Content |
|--------|----------|---------|
| Serial input | `port_mem.serial_rx` ring | Keystrokes, paste data |
| Hardware state | MEMFS GPIO/analog slots | Pin values, ADC readings |
| Events | Event ring | Wake, resize, pin interrupts |
| Timing | `chassis_frame()` args | `time_us`, `budget_us` |

### Outbound (C -> JS, after frame)

| Stream | Location | Content |
|--------|----------|---------|
| Serial output | `port_mem.serial_tx` ring | REPL prompts, print(), tracebacks |
| Hardware state | MEMFS GPIO/neopixel slots | Pin outputs, LED colors |
| Dirty flags | `port_mem` bitmasks | Which slots changed this frame |
| Status | `port_mem.state` | Phase, elapsed_us, wakeup_ms |

### Control (synchronous, any time)

WASM exports act as USB control transfers — synchronous commands
that don't wait for the next frame:

- `chassis_init()` — device reset
- `cp_start_repl()` / `cp_start_code()` — mode changes
- `cp_ctrl_c()` / `cp_ctrl_d()` — signal injection
- `cp_port_memory_addr()` — descriptor query

### The Packet Protocol

The [packet protocol](packet-protocol.md) documents the **logical
contract** — what data flows per frame and what it means.  The
physical transport is direct memory access (port_mem reads/writes).
JSON serialization is only used for secondary transports (Worker
postMessage, WebSocket to external hardware).

```
Packet protocol (logical)          Physical realization
-----------------------------      --------------------
serial.rx                     -->  port_mem.serial_rx ring
serial.tx                     <--  port_mem.serial_tx ring
gpio, analog, events          -->  MEMFS slots + dirty flags
gpio_dirty, display_dirty     <--  MEMFS dirty bitmasks
status, phase, wakeup_ms      <--  port_mem.state fields
budget_us, time_ms            -->  chassis_frame() arguments
control commands              -->  WASM exports
```

## WASI stdin/stdout — Diagnostic Only

WASI file descriptors are NOT part of the data bus.  Their role
depends on the runtime environment:

### Browser (primary target)

WASI stdout and stderr both map to `console.log` / `console.error`.
Diagnostic only.  No user-facing data.  No infrastructure protocol.

This gives us an **emergency killswitch**: if the C code enters an
infinite loop that defeats the budget system (e.g., a tight loop
in a C function that doesn't hit HOOK_LOOP), a watchdog in the JS
wrapper can detect that `chassis_frame()` hasn't returned within
a hard timeout and terminate the Worker.  Diagnostic output on
stderr before the kill provides forensic data:

```js
// JS wrapper pseudocode
const timer = setTimeout(() => {
    console.error('frame watchdog: killing unresponsive worker');
    worker.terminate();
}, HARD_TIMEOUT_MS);

worker.postMessage({ type: 'frame', ... });
// worker calls chassis_frame() internally
// on response, clearTimeout(timer)
```

The C side can also write to stderr as a last resort before a
crash — `write(STDERR_FILENO, ...)` reaches `console.error`
even if port_mem is corrupted.

### Node.js CLI

Two options, selectable by build flag or runtime configuration:

**Option A (recommended)**: Same as browser.  The Node.js harness
bridges port_mem rings to the host terminal:

```js
// Node.js harness
process.stdin.on('data', (chunk) => {
    // Write to port_mem.serial_rx ring
    writeToRxRing(chunk);
});

// After each chassis_frame():
const output = drainTxRing();
process.stdout.write(output);
```

WASI stdout is diagnostic (`console.error`).  Same code paths
as browser.  One serial implementation for both environments.

**Option B (fallback)**: Wire WASI stdin/stdout as a secondary
serial channel via `board_serial_*()`.  Useful if the Node.js
harness can't access port_mem directly (e.g., running under
wasmtime without a JS wrapper).  Controlled by a build flag
(`CIRCUITPY_WASI_SERIAL`).

### Web Worker

When the VM runs in a dedicated Worker, `chassis_frame()` is
called inside the Worker.  The main thread communicates via
`postMessage`.  The Worker's JS glue writes to port_mem before
calling `chassis_frame()` and reads port_mem after — same
pattern, different JS context.  WASI stdout in the Worker maps
to the Worker's `console` (visible in DevTools).

## Board Serial — Clean by Construction

Because port_mem.serial_tx carries ONLY supervisor/VM output
(REPL prompts, `print()`, tracebacks, banner), a JS module
rendering a terminal widget can display every byte with zero
filtering.  No infrastructure noise ever enters the serial rings.

This is clean by construction, not by filtering:

- WASM exports handle control signals (no stdin commands to leak)
- MEMFS slots handle hardware state (no state dumps in serial)
- Dirty flags handle change notification (no poll responses in serial)
- WASI stdout handles diagnostics (no debug spew in serial)

The serial rings carry what a real board's USB CDC serial carries:
the user's conversation with CircuitPython.

## The "Board Writes Its Own UI" Pattern

This clean serial channel enables a powerful pattern: Python code
drives browser UI updates without knowing about the browser.

When Python writes to hardware (NeoPixels, LEDs, display), the
common-hal layer updates MEMFS slots and sets dirty flags.  After
`chassis_frame()` returns, the JS wrapper reads the dirty flags
and reacts:

```
Python: pixels[3] = (255, 0, 0)
  -> common-hal writes RGB to MEMFS neopixel slot 3
  -> sets neopixel_dirty flag

JS (after frame):
  -> sees neopixel_dirty
  -> reads slot 3: RGB(255, 0, 0)
  -> clones SVG glow template, applies red color
  -> attaches animation to board SVG element
```

The board's `definition.json` and `board.svg` provide templates
(uncolored LED shapes, pin labels, component outlines).  The dirty
flags tell JS what changed.  JS brings the board to life visually.
Python writes hardware registers — it doesn't generate HTML.

## Upstream Serial Integration

Upstream CircuitPython's `supervisor/shared/serial.c` multiplexes
across USB CDC, UART, BLE, WebSocket, and port-specific channels.
We plug in via the `port_serial_*` weak functions:

```c
// CIRCUITPY_PORT_SERIAL = 1
port_serial_connected()         -> true (JS host always present)
port_serial_bytes_available()   -> serial_rx_available() from port_mem ring
port_serial_read()              -> consume one byte from port_mem.serial_rx
port_serial_write_substring()   -> serial_tx_write() to port_mem.serial_tx
```

The supervisor sees a serial port.  It doesn't know about packets,
WASM, JS, or browsers.  `mp_hal_stdin_rx_chr()` calls
`serial_read()` which calls `port_serial_read()` which reads from
the ring.  Clean layering.

## What This Replaces

The current implementation conflates board serial with WASI IO:

- `supervisor/serial.c` (our port-local version) writes REPL
  output to WASI stdout via `write(STDOUT_FILENO)`
- `serial_push_byte()` feeds a 256-byte linear buffer separate
  from port_mem
- `port/serial.c` has ring buffer helpers that nothing uses

Consolidation:

| Current | After |
|---------|-------|
| 256-byte `_rx_buf` in supervisor/serial.c | `port_mem.serial_rx` (4K ring) |
| `write(STDOUT_FILENO)` for REPL output | `port_mem.serial_tx` (4K ring) |
| `port/serial.c` ring helpers (unused) | Wired via `port_serial_*()` |
| Our 190-line supervisor/serial.c | Upstream `supervisor/shared/serial.c` |
| `cp_serial_push()` WASM export | JS writes `port_mem.serial_rx` directly |

Net effect: delete our serial.c replacement, add ~30 lines of
`port_serial_*()` implementations, enable `CIRCUITPY_PORT_SERIAL`,
use the upstream multiplexer unchanged.

## Future Considerations

- **Ring overflow**: If serial_tx fills (4K - 8 = 4088 bytes),
  output is lost.  Monitor in practice; increase ring size or
  add backpressure signaling if needed.

- **WASI serial fallback**: If a runtime can't access port_mem
  (bare wasmtime, no JS wrapper), `CIRCUITPY_WASI_SERIAL` build
  flag enables `board_serial_*()` over WASI stdin/stdout as a
  secondary channel.  Not the primary path.

- **Worker SharedArrayBuffer**: If the Worker can't use direct
  memory access (cross-origin isolation restrictions), fall back
  to postMessage with the JSON packet transport.  The port_mem
  rings still exist; the Worker JS glue copies to/from them.
