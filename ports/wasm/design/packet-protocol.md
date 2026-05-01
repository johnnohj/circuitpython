# Frame Packet Protocol — JS ↔ Wasm Layer

**Created**: 2026-04-28
**Status**: Draft — resolved open questions, ready for implementation

## Context

The wasm layer communicates with JS through a per-frame packet
protocol.  Each `chassis_frame()` call receives one inbound packet
(JS→C) and produces one outbound packet (C→JS).  This is the frame
boundary contract — the inter-processor communication (IPC) format
between the VM "core" and the JS "coprocessor" (see
`design/js-coprocessor.md`).

The protocol is JSON-based for readability, debuggability, and ease
of integration with browser DevTools, Node.js tooling, and future
external hardware bridges.

## Prior Art Reviewed

### Adafruit IO — Feed Model
- **Data model**: Named feeds with key, value, metadata
- **Lesson**: Simple flat key-value with types works well for IoT data
- **Applicable**: Our GPIO pins are essentially named feeds
  (pin 5 → value 1, direction input, pull up)

### WipperSnapper — Component Definitions + Event Ring
- **Protocol**: MQTT + protobuf for cloud; U2IF opcodes for hardware
- **Data model**: Component definitions as JSON metadata; 8-byte
  binary event packets for state changes
- **Lesson**: Separate metadata (what hardware exists) from runtime
  data (what values it has right now).  Hardware is data, not behavior.
- **Applicable**: Our `definition.json` describes the board;
  the packet carries runtime state changes

### JACDAC — Service/Register Model
- **Protocol**: Binary frames on a single-wire bus
- **Data model**: Services expose registers (polled state), events
  (change notifications), and commands (actions)
- **Lesson**: The three-way split — registers/events/commands — maps
  cleanly to our MEMFS slots / event ring / WASM imports
- **Applicable**: Our packet should carry register updates (state)
  and events (changes), while commands remain WASM exports

## Design Principles

1. **Per-frame**: One inbound JSON object, one outbound JSON object.
   The frame boundary is the packet boundary.

2. **State, not commands**: The packet carries the current state of
   things, not instructions.  "Pin 5 is now HIGH" not "set pin 5 HIGH."
   Commands (Ctrl-C, start code, soft reboot) go through WASM exports.

3. **Delta encoding**: Only include what changed.  An empty `gpio`
   object means no pin changes this frame.

4. **Self-describing**: Each packet includes enough context that a
   tool can interpret it without prior state (frame number, timestamp,
   protocol version).

5. **Extensible**: New fields can be added without breaking existing
   consumers.  Unknown fields are ignored.

6. **Versioned**: Both packets include a `v` field for forward
   compatibility.

## Inbound Packet (JS → C, before frame)

JS builds this object and passes it to the frame function.  In the
current architecture, most of this data is written to MEMFS/port_mem
before calling `chassis_frame()`.  The packet formalizes what JS
must provide.

```json
{
  "v": 1,
  "frame": 42,
  "time_ms": 1234.567,
  "budget_us": 10000,
  "visibility": "visible",

  "serial": {
    "rx": "print('hello')\n"
  },

  "gpio": {
    "5": { "value": 1 },
    "21": { "value": 0 }
  },

  "analog": {
    "14": { "value": 2048 }
  },

  "events": [
    { "type": "wake" }
  ]
}
```

### Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `v` | int | yes | Protocol version (currently 1) |
| `frame` | int | yes | Monotonic frame counter (JS assigns) |
| `time_ms` | float | yes | `performance.now()` timestamp |
| `budget_us` | int | no | Frame budget in microseconds (0 = default 10ms) |
| `visibility` | string | no | `"visible"`, `"hidden"`, `"backgrounded"`. Hints to C that the tab/window state changed. C may adjust behavior (skip display updates, reduce tick frequency). Default `"visible"`. |
| `serial.rx` | string | no | Characters to feed into the serial rx buffer. All keypresses observed since last frame — character-at-a-time, not line-buffered. The wasm layer decides when to feed them to readline. |
| `gpio` | object | no | Pin index → state delta. Only changed pins. |
| `gpio[N].value` | int | no | 0 or 1 |
| `gpio[N].pull` | int | no | 0=none, 1=up, 2=down |
| `analog` | object | no | Channel index → state delta |
| `analog[N].value` | int | no | 0-65535 (16-bit ADC value) |
| `events` | array | no | Additional events (wake, resize, etc.) |

### How JS populates this

In practice, JS doesn't need to serialize JSON and pass it across
the WASM boundary.  Instead, JS writes directly to MEMFS/port_mem:

1. Write `serial.rx` bytes to `port_mem.serial_rx` ring buffer
2. Write `gpio[N]` to `gpio_slot(N)` + call `hal_mark_gpio_dirty(N)`
3. Write `analog[N]` to `analog_slot(N)` + call `hal_mark_analog_dirty(N)`
4. Write events to the event ring
5. Call `chassis_frame(time_us, budget_us)`

The JSON schema above is the **logical contract** — it documents
what data flows JS→C each frame, regardless of transport.  For
external consumers (debug tools, test harnesses, external hardware
bridges), the JSON form may be used literally.

## Outbound Packet (C → JS, after frame)

C produces this as the frame result.  JS reads it from port_mem
after `chassis_frame()` returns.

```json
{
  "v": 1,
  "frame": 42,
  "status": "done",
  "elapsed_us": 3200,
  "wakeup_ms": 0,
  "phase": "idle",

  "serial": {
    "tx": "hello\n"
  },

  "gpio_dirty": [13],
  "analog_dirty": [],
  "display_dirty": false,
  "neopixel_dirty": false
}
```

### Fields

| Field | Type | Always | Description |
|-------|------|--------|-------------|
| `v` | int | yes | Protocol version (currently 1) |
| `frame` | int | yes | Echo of inbound frame counter |
| `status` | string | yes | `"done"`, `"yield"`, `"error"` |
| `elapsed_us` | int | yes | Wall-clock time spent in this frame |
| `wakeup_ms` | int | yes | C's requested delay before next frame. 0 = wake on next event. JS applies its own policy on top (rAF pacing, background tab throttling). |
| `phase` | string | yes | Current lifecycle phase name |
| `serial.tx` | string | no | Characters produced by the VM this frame |
| `gpio_dirty` | int[] | no | Pin indices that C wrote this frame |
| `analog_dirty` | int[] | no | Channel indices that C wrote |
| `display_dirty` | bool | no | True if framebuffer was updated |
| `neopixel_dirty` | bool | no | True if neopixel data changed |

### How JS reads this

1. Read `status` from `port_mem.state.status`
2. Read `serial.tx` by draining `port_mem.serial_tx` ring buffer
3. Read `gpio_dirty` from `port_mem.hal_gpio_dirty` bitmask
4. Read `display_dirty` from framebuffer frame_count change
5. For each dirty pin, read the full 12-byte slot for updated state

## Transport Layers

The packet protocol is transport-agnostic.  Three transports are
planned:

### 1. Direct memory (primary — browser + Node.js)
- JS writes to MEMFS/port_mem before frame, reads after
- Zero-copy, zero-serialization
- The JSON schema documents the logical contract but data moves
  as raw bytes in linear memory

### 2. JSON over postMessage (Web Worker isolation)
- When the VM runs in a Web Worker, the main thread sends/receives
  JSON packets via `postMessage`
- The Worker unpacks the JSON into MEMFS before calling `chassis_frame`
- Structured clone handles the transfer
- Display data is NOT part of the packet — OffscreenCanvas handles
  display rendering within the Worker

### 3. JSON over WebSocket/serial (external hardware bridge)
- For connecting to real hardware via WebUSB/WebSerial
- Same packet format, serialized as JSON text
- The weBlinka bridge translates between packets and U2IF commands
- Future: JACDAC-style service discovery over this channel
- Display data is out of scope for this transport unless
  chunked/streaming is viable

## Relationship to MEMFS

The packet protocol does NOT replace MEMFS.  MEMFS remains the
single source of truth for all hardware state.  The packet protocol
describes **what changed** at the frame boundary — it's a delta
notification, not a state snapshot.

```
JS writes to MEMFS  →  packet documents the write  →  C reads from MEMFS
C writes to MEMFS   →  packet documents the write  →  JS reads from MEMFS
```

The packet is metadata about MEMFS changes, not a replacement for them.

## Relationship to Event Ring

Events that don't map to MEMFS state changes (wake, resize, ctrl-c)
go through the event ring, which the packet's `events` array mirrors.
Pin state changes go through MEMFS directly + dirty flags — the event
ring is for notifications that don't have a natural MEMFS home.

## Relationship to Hardware Events / FFI Proxies

Hardware events on pins (e.g., a GPIO change triggered by a DOM
click on the board SVG) are delivered through FFI/proxy event-firing
functions.  These events carry the data they need — the pin index,
the new value, and any metadata.  The packet does NOT need to carry
full pin state every frame because:

1. JS writes the new state to MEMFS before the frame
2. JS marks the pin dirty
3. The event itself (via event ring or FFI proxy) carries the change
4. C reads the full slot from MEMFS only for dirty pins

This means the packet's `gpio` field (inbound) and `gpio_dirty` field
(outbound) are sufficient — no full-state-per-frame parsing needed.

## Serial Input Model

Frames are live updates.  All keypresses observed since the last
frame are included in `serial.rx`.  The wasm layer (C side) owns
the decision of how to present them to the VM:

- **Character-at-a-time**: feed each byte to the serial rx ring.
  C-side readline handles line editing, tab completion, hints.
- **Line-buffered**: accumulate until Enter, then feed the complete
  line.  Only if C-side readline can't run (unlikely).
- **Paste mode**: Ctrl-E enters paste mode; Ctrl-D ends it.
  The wasm layer should investigate and support this escape sequence.

In all cases, keypresses flow: JS keyboard event → `serial.rx` in
packet → `port_mem.serial_rx` ring buffer → background task feeds
to C-side readline → readline produces a line → compile + execute.

This area will need testing and refinement.

## Timing Model

### C side: `wakeup_ms`
C requests when it next needs a frame via `wakeup_ms`:
- `0` = no specific deadline; wake on next event (pin change,
  keystroke, etc.)
- `N` = wake in N milliseconds (e.g., `time.sleep(1)` sets 1000)

### JS side: timing budget ("hot potato")
JS has its own timing policy layered on top of C's request:
- **Visible tab**: `requestAnimationFrame` pacing (~16ms), or
  `setTimeout(wakeup_ms)` if C requested a specific delay
- **Hidden/backgrounded tab**: throttled — may slow to 1fps or
  pause entirely.  JS sends `visibility: "hidden"` in the inbound
  packet so C can skip display updates, reduce tick frequency, etc.
- **Worker**: no rAF; uses `setTimeout` exclusively, paced by
  `wakeup_ms` or a default interval

The wakeup is automatic — JS always calls back.  C never needs to
request a frame explicitly (though `ffi_request_frame()` exists as
a nudge for edge cases).
