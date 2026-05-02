# Distributed Architecture: Sync Bus as Local Broker

## Overview

The CircuitPython browser simulator is built from three independent
pieces connected by a shared sync bus.  Each piece runs at its own
pace.  No frame coordination — readers sample latest state, writers
write whenever they have new data.

## The three pieces

### 1. VM (Web Worker)

`circuitpython.wasm` — the full CircuitPython VM.  Runs in a Web
Worker so it can block on `time.sleep`, `input()`, and serial reads
without freezing the UI.  Communicates with the sync bus by reading
and writing named regions.

- Reads: `/serial/rx`, `/gpio` (input values), `/sensors/*`
- Writes: `/serial/tx`, `/gpio` (output values, claims), `/neopixel`,
  `/framebuffer`, `/i2c/*`, `/spi/*`

### 2. Hardware (iframe, Emscripten)

`hardware.mjs` + `hardware.wasm` — built with Emscripten for DOM/SDL
access.  Runs in an `<iframe>` or `<embed>`.  Owns the visual
rendering of the board: pin layout, NeoPixel LEDs, display, buttons,
connectors.  Captures user interaction (mouse/touch on board elements)
and writes to the sync bus.

- Reads: `/gpio`, `/neopixel`, `/framebuffer`, `/i2c/*` (to simulate
  peripherals like OLEDs, sensors)
- Writes: `/gpio` (button presses, switch toggles)
- Renders: SDL2 surface → canvas (board layout, LEDs, display)
- Handles: mouse events on interactive board elements

Uses Emscripten's SDL2 library for the canvas surface.  The board
layout can be procedural (from `definition.json`) or from a compiled
SVG.  Each board variant is a different hardware binary.

### 3. Parent page (main thread)

`circuitpython.mjs` — the integration layer.  Provides:

- **Code editor** — textarea or CodeMirror, writes files to sync bus
- **Serial monitor** — reads `/serial/tx`, displays output, captures
  keystrokes → `/serial/rx`
- **Controls** — Run, Stop, REPL buttons → commands to VM Worker
- **Sensor panels** — sliders, waveform generators → `/sensors/*`
- **External hardware bridges** — WebSerial, WebUSB, WebBLE → sync bus
- **Cloud bridges** — Adafruit IO, MQTT → sync bus feeds

The parent page owns the sync bus instance and distributes ports to
the Worker and iframe.

## Sync bus regions

```
/serial/tx          ring     VM → parent        REPL/print output
/serial/rx          ring     parent → VM        keyboard input
/gpio               slots    VM ↔ hardware      pin state (32 pins × 12B)
/neopixel           slab     VM → hardware      pixel data (header + GRB)
/framebuffer        slab     VM → hardware      display (RGB565)
/analog             slots    VM ↔ parent         ADC values (8 ch × 4B)
/i2c/0              ring     VM ↔ hardware      I2C bus 0 transactions
/i2c/1              ring     VM ↔ hardware      I2C bus 1 transactions
/spi/0              ring     VM ↔ hardware      SPI bus 0 transactions
/uart/0             ring     VM ↔ external      UART via WebSerial bridge
/sensors/bme280     slots    parent → VM        temperature/humidity/pressure
/sensors/lis3dh     slots    parent → VM        accelerometer XYZ
/aio/feeds          ring     parent ↔ cloud     Adafruit IO / MQTT
/ble/gatt           ring     parent ↔ external  WebBLE GATT operations
/commands           ring     parent → VM        exec, stop, ctrl-c, ctrl-d
```

### Region types

- **ring**: FIFO byte stream.  Nothing is lost, reader drains at own
  pace.  Used for serial, I2C transactions, commands.
- **slots**: fixed-size records with per-slot dirty bitmask.  Reader
  sees latest value, may miss intermediates.  Used for GPIO, sensors.
- **slab**: opaque byte range with sequence number.  Reader sees
  latest snapshot.  Used for framebuffer, neopixel bulk data.

### Dirty tracking

Each region has a sequence number that increments on every write.
Readers compare against their last-seen sequence to decide whether
to do expensive work (render, parse, copy).  The bus also has a
global sequence number — if it hasn't changed, nothing on the bus
has changed.

## Transport

### SharedArrayBuffer (preferred)

All three pieces share the same `WebAssembly.Memory`:

```
const memory = new WebAssembly.Memory({
    initial: 256, maximum: 256, shared: true
});
```

The VM Worker, hardware iframe, and parent page all create typed
array views over this shared buffer.  Reads and writes are direct
memory access — zero copy, zero serialization.  Dirty flags use
`Atomics.load`/`Atomics.store` for coherence.

`Atomics.wait`/`Atomics.notify` provide sleep/wake for the VM:
- `time.sleep(0.1)` → `Atomics.wait(wake, 0, 0, 100)`
- Button press → `Atomics.store(wake, 0, 1); Atomics.notify(wake, 0)`

Requires `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp` headers.

### postMessage (fallback)

Without SAB, each piece has its own memory.  Communication uses
`postMessage` with `Transferable` ArrayBuffers:

- Worker posts outbound packet each frame (serial bytes, GPIO
  snapshot, neopixel snapshot, framebuffer if dirty)
- Parent applies packet to hardware.wasm and renders
- Input events sent to Worker as messages
- Framebuffer uses Transferable (zero-copy ownership transfer)

The sync bus API is the same either way — `port.read()`, `port.write()`,
`port.drain()`, `port.changed()`.  The transport is selected at
bus creation time.

## Sensor panels

The parent page hosts sensor control UI:

```
┌─ Temperature ──────────────────────┐
│  [slider: -40°C ────●──── 85°C]   │
│  waveform: [sine ▾] period: [1s]  │
│  current: 23.5°C                   │
└────────────────────────────────────┘
```

The slider/waveform writes to `/sensors/bme280` (temperature slot).
The VM's common-hal `analogio` or `adafruit_bme280` driver reads
from this slot.  The board definition (`definition.json`) declares
which sensors are present and their I2C addresses.

Waveform generators produce continuous signals:
- Sine, triangle, sawtooth, square, noise
- Configurable period, amplitude, offset
- Write to sensor slots at bus-native rate

## External hardware bridges

### WebSerial → `/uart/0`

```js
const port = await navigator.serial.requestPort();
await port.open({ baudRate: 115200 });
const reader = port.readable.getReader();
// Read from real hardware → write to sync bus
while (true) {
    const { value } = await reader.read();
    busPort.push('/uart/0', value);
}
// Read from sync bus → write to real hardware
const writer = port.writable.getWriter();
setInterval(() => {
    const data = busPort.drain('/uart/0/tx');
    if (data.length) writer.write(data);
}, 16);
```

### WebUSB → `/usb/0`

Same pattern — bridge between `USBDevice.transferIn/transferOut`
and sync bus ring regions.

### WebBLE → `/ble/gatt`

GATT characteristics map to sync bus slots.  A characteristic
write from the VM becomes a slot write on the bus.  The parent
page's BLE bridge reads the slot and calls
`characteristic.writeValue()`.

## Adafruit IO / MQTT integration

The parent page can subscribe to Adafruit IO feeds and write
values to sync bus sensor slots:

```js
const client = new Paho.MQTT.Client(host, port, clientId);
client.onMessageArrived = (msg) => {
    const feed = msg.destinationName;  // e.g. 'temperature'
    const value = parseFloat(msg.payloadString);
    busPort.writeSlot('/aio/feeds', feedIndex, encode(value));
};
```

The VM reads these as if they were local sensors.  The definition.json
maps feed names to sensor slots.

## Board variants

Each board variant is a different hardware binary:

```
boards/
  wasm_browser/
    definition.json    ← pin layout, components, sensors
    magic.json         ← pin name → number mapping
    hardware.wasm      ← Emscripten SDL2 renderer
  feather_rp2040/
    definition.json
    magic.json
    hardware.wasm      ← different layout, different peripherals
  clue_nrf52840/
    definition.json
    magic.json
    hardware.wasm      ← has TFT display, buttons, sensors
```

The parent page loads the board's `definition.json` to configure
sensor panels and pin labels.  The hardware.wasm renders the board
visually.  The VM binary (`circuitpython.wasm`) is the same for all
boards — only the `mpconfigboard.h` pin definitions differ.

## Component protocol

Board components (sensors, displays, peripherals) communicate over
the I2C/SPI bus regions using the same wire protocol as real hardware.
An `adafruit_bme280` Python driver sends I2C transactions that appear
in the `/i2c/0` ring.  The hardware binary reads the ring, recognizes
the BME280 address (0x77), and responds with simulated sensor data
from the `/sensors/bme280` slots.

This means the same Python driver code works for:
- Real hardware (via common-hal I2C → physical I2C bus)
- Simulated hardware (via common-hal I2C → sync bus → hardware.wasm)
- External hardware (via common-hal I2C → sync bus → WebUSB bridge)

The sync bus is the universal transport.  The endpoints determine
the semantics.

## Data routing (current implementation)

Two distinct paths:

### Direct path: framebuffer only

```
VM displayio → framebuffer in port_mem → Worker copies if dirty →
  postMessage (Transferable) → main thread → RGB565→RGBA → canvas
```

The framebuffer is the only direct-connect path.  It's pure pixels
(345KB per frame at 480×360), has exactly one consumer (the canvas),
and no external bridge or sensor panel cares about raw RGB565 data.
Routing it through the sync bus would add a copy for no benefit.

### Broker path: everything else

All other data flows through the sync bus broker:

```
VM port_mem → Worker reads → postMessage → main thread →
  sync bus regions → consumers (iframe, serial panel, sensor
  panels, WebSerial bridge, cloud feeds, protocol inspector)
```

- **Serial TX/RX** → bus rings → serial panel reads, WebSerial
  bridge may also forward
- **GPIO** → bus slots → iframe reads for rendering, sensor panels
  see pin state, WebSerial bridge forwards to real hardware
- **NeoPixel** → bus slab → iframe reads for LED rendering,
  WebSerial bridge may forward to real LEDs
- **Analog** → bus slots → sensor panels write, VM reads
- **Protocol messages** → fd 4 → Worker → bus → protocol inspector,
  external bridges, cloud feeds

The broker provides:
- **Dirty tracking** — consumers skip work when nothing changed
- **Multiple consumers** — GPIO data serves the iframe AND the
  protocol inspector AND a WebSerial bridge simultaneously
- **Attachment point** — new consumers (cloud feed, BLE bridge)
  attach to the bus without modifying the Worker or iframe
- **Coordination** — sensor panels write, the VM reads, the iframe
  renders, all through the same bus regions

### fd 4 protocol channel

Protocol messages bypass serial entirely:
- fd 1 (serial) → serial_write_substring → displayio terminal +
  serial_tx ring → user sees print output
- fd 4 (protocol) → WASI fd_write → JS callback → protocol
  channel → invisible to user, visible to external consumers

### weBlinka as coordination layer

`weBlinka` is the name for the compatibility/coordination layer
that the sync bus enables.  It is the JS-side logic that:

1. Manages the sync bus instance
2. Attaches sensor panels, external bridges, cloud feeds
3. Routes protocol messages to the appropriate responder
4. Provides a uniform API regardless of hardware source

Like JACDAC (added as a fourth bus alongside I2C/SPI/UART),
weBlinka operates "under the hood" of the standard bus interfaces.
The VM thinks it's doing I2C — weBlinka translates to protocol
messages.  The VM thinks it's reading a real sensor — weBlinka
provides data from a slider, a WebSerial device, or an MQTT feed.

### Bus fidelity

We do NOT simulate:
- I2C clock timing, stretching, arbitration
- SPI clock phase, polarity, bit ordering
- UART baud rate timing, parity, flow control
- Electrical characteristics, voltage levels

We DO simulate:
- Transaction semantics (address, register, read/write, data)
- Device presence (which addresses respond on which bus)
- Register maps (which registers exist, their default values)
- Data encoding (how temperature becomes raw register bytes)

This level of fidelity matches what Python drivers actually use.
`adafruit_bme280` reads register 0xFA and decodes 3 bytes as
temperature.  It doesn't care about clock speed or pull-up
resistance.  We provide the register bytes; the driver does the
math.
