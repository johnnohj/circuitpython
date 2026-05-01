# Board-as-UI: Visual Hardware from Definition Files

**Status**: Design (2026-04-26)
**Depends on**: abort-resume, MEMFS-in-linear-memory, port chassis

## Problem

CircuitPython runs on physical boards with physical hardware.  Users write
code like `pin = digitalio.DigitalInOut(board.D5)` and see LEDs light up,
read button presses, drive NeoPixels.  A browser port needs the same
experience: the same API, the same feedback loop, the same "it just works"
feeling — without any physical hardware.

The user should never write `import js` or call browser APIs.  They write
CircuitPython.  The board happens to be in a browser.

## Solution

A `definition.json` + `board.svg` pair fully describes a board's hardware
and visual layout.  JS reads these at load time, renders an interactive
board image, and wires DOM events to MEMFS endpoints.  Common-hal modules
read and write MEMFS — the same code path they'd use on any port.  The
proxy/FFI layer is internal plumbing: common-hal uses WASM imports to
coordinate with JS, but the user never sees this.

This is how `modasyncio` uses thread primitives internally while exposing
`asyncio.sleep()` to users.  The browser is the hardware.  The port is the
firmware.  The user writes the same code.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  User Python code                                       │
│  pin = digitalio.DigitalInOut(board.D5)                 │
│  pin.direction = Direction.INPUT                        │
│  pin.pull = Pull.UP                                     │
│  if not pin.value: print("pressed!")                    │
└──────────────────────┬──────────────────────────────────┘
                       │ (shared-bindings)
                       ▼
┌─────────────────────────────────────────────────────────┐
│  common-hal/digitalio/DigitalInOut.c                    │
│                                                         │
│  construct:  claim_pin(5)                               │
│              hal_set_role(5, HAL_ROLE_DIGITAL_IN)       │
│              port_register_pin_listener(5)  ← WASM imp  │
│                                                         │
│  get_value:  read MEMFS /hal/gpio slot[5]               │
│              check HAL_FLAG_LATCHED                     │
│              return latched or current value             │
│                                                         │
│  set_value:  write MEMFS /hal/gpio slot[5]              │
│              set HAL_FLAG_C_WROTE                        │
└──────────────────────┬──────────────────────────────────┘
                       │ (MEMFS = linear memory)
                       ▼
┌─────────────────────────────────────────────────────────┐
│  port_mem (WASM linear memory)                          │
│                                                         │
│  /hal/gpio  ─── 12 bytes × 64 pins                     │
│  /hal/analog ── 4 bytes × 64 pins                      │
│  /hal/pwm ───── 8 bytes × 64 pins                      │
│  /hal/neopixel  pixel color data                        │
│  /hal/display   framebuffer (RGB565)                    │
│  /port/event_ring  JS→C event notifications             │
└──────────────────────┬──────────────────────────────────┘
                       │ (same bytes, zero-copy views)
                       ▼
┌─────────────────────────────────────────────────────────┐
│  JS board renderer                                      │
│                                                         │
│  Reads definition.json + board.svg at load time         │
│  Renders interactive board image into DOM               │
│  Attaches listeners when C calls register_pin_listener  │
│  Writes pin changes to MEMFS + pushes event ring entry  │
│  Reads MEMFS to update visuals (LED state, PWM duty,    │
│  NeoPixel colors, display framebuffer)                  │
└─────────────────────────────────────────────────────────┘
```

## Board definition files

### definition.json

Already exists at `boards/wasm_browser/definition.json`.  Describes:

- **Identity**: boardName, displayName, vendor, mcuName
- **Pins**: name, GPIO id, capabilities (digital, analog, pwm, i2c, spi,
  uart, touch), visual position (x, y, side)
- **Aliases**: D13 = LED = LED_BUILTIN, A4 = SDA
- **Power**: 3V3, 5V, GND positions
- **Buses**: I2C (sda/scl/freq), SPI (mosi/miso/sck), UART (tx/rx/baud)
- **Visual**: SVG filename, board dimensions, pin spacing

### board.svg

An SVG image of the board.  Pin locations are marked with `data-pin-id`
attributes matching definition.json pin names:

```xml
<svg viewBox="0 0 400 220">
  <!-- Board outline -->
  <rect class="board-body" ... />

  <!-- Pin pads — interactive targets -->
  <circle data-pin-id="D5" cx="140" cy="20" r="5" class="pin-pad" />
  <circle data-pin-id="BUTTON_A" cx="60" cy="110" r="12" class="pin-button" />

  <!-- On-board components — updated by JS -->
  <circle data-component="LED" cx="300" cy="60" r="4" class="led-indicator" />
  <rect data-component="DISPLAY" x="120" y="60" width="160" height="100" />
  <g data-component="NEOPIXEL">
    <circle data-pixel="0" cx="200" cy="160" r="4" />
  </g>
</svg>
```

The SVG is pure artwork.  JS finds elements by `data-*` attributes and
attaches behavior.  Board artists don't write code.

### components/*.json

External components (BME280, NeoPixel strip, SSD1306) have their own
definition files.  These describe I2C addresses, register maps, sensor
ranges, and UI widgets.  JS uses them to render connected peripherals
and simulate register-level I2C/SPI responses.

## Boot sequence

### 1. JS loads board definition

```js
const def = await fetch('boards/wasm_browser/definition.json').then(r => r.json());
const svg = await fetch('boards/wasm_browser/board.svg').then(r => r.text());
```

### 2. JS renders board into DOM

```js
boardContainer.innerHTML = svg;

// Populate MEMFS template slots from definition
for (const pin of def.pins) {
    const category = capabilityToCategory(pin.capabilities);
    memfs.writeSlot('/hal/gpio', pin.id, {
        enabled: 1,
        direction: HAL_DIR_INPUT,
        value: 0,
        pull: HAL_PULL_NONE,
        role: HAL_ROLE_UNCLAIMED,
        flags: 0,
        category: category,
        latched: 0
    });
}
```

### 3. WASM module loads and C calls hal_init

C reads the pre-populated MEMFS slots.  `hal_init_pin_categories()` is
already done — JS wrote the template state from definition.json before
C started.  The board table in C (`board_pins.c`) maps names to GPIO
numbers for `import board`.

### 4. Python claims a pin

```python
import board, digitalio
pin = digitalio.DigitalInOut(board.BUTTON_A)
pin.direction = digitalio.Direction.INPUT
pin.pull = digitalio.Pull.UP
```

Common-hal `construct()`:
1. Claims pin 21 (BUTTON_A's GPIO id)
2. Sets `role = HAL_ROLE_DIGITAL_IN` in MEMFS slot
3. Calls `port_register_pin_listener(21)` — a WASM import

### 5. JS receives listener registration

```js
registerPinListener(pinId) {
    const pin = this.definition.pins.find(p => p.id === pinId);
    const el = this.svg.querySelector(`[data-pin-id="${pin.name}"]`);
    if (!el) return;  // pin has no visual element

    // Read current role from MEMFS to determine interaction mode
    const slot = memfs.readSlot('/hal/gpio', pinId);

    if (slot.role === HAL_ROLE_DIGITAL_IN) {
        // Input pin: user can click to toggle / press-and-hold
        el.classList.add('pin-active', 'pin-input');
        el.addEventListener('pointerdown', () => {
            this.pushEvent(SH_EVT_HW_CHANGE, pinId, 1);
        });
        el.addEventListener('pointerup', () => {
            this.pushEvent(SH_EVT_HW_CHANGE, pinId, 0);
        });
    }
}
```

### 6. User interacts with board image

User clicks BUTTON_A in the rendered SVG.  JS:
1. Writes value=1 to MEMFS `/hal/gpio` slot 21
2. Sets `HAL_FLAG_JS_WROTE | HAL_FLAG_LATCHED`
3. Pushes `SH_EVT_HW_CHANGE` to event ring
4. Kicks the frame loop if idle

### 7. C drains event, Python reads value

Next `chassis_frame()`:
1. `port_drain_events()` — sees HW_CHANGE for pin 21
2. VM resumes, Python calls `pin.value`
3. `common_hal_digitalio_digitalinout_get_value()` reads MEMFS slot
4. Returns latched value (1 = pressed), clears latch flag
5. Python prints "pressed!"

## Output: C writes, JS reads

The reverse direction — Python sets an LED, NeoPixel color, or PWM duty —
follows the same MEMFS pattern but in the other direction.

### LED

```python
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT
led.value = True
```

Common-hal writes `value=1, HAL_FLAG_C_WROTE` to MEMFS slot 13 (D13/LED).

JS reads dirty flags after each frame:

```js
afterFrame() {
    const dirty = this.exports.hal_gpio_drain_dirty();
    for (const pinId of bitsOf(dirty)) {
        const slot = memfs.readSlot('/hal/gpio', pinId);
        if (slot.flags & HAL_FLAG_C_WROTE) {
            this.updatePinVisual(pinId, slot);
        }
    }
}

updatePinVisual(pinId, slot) {
    const pin = this.definition.pins.find(p => p.id === pinId);
    const el = this.svg.querySelector(`[data-pin-id="${pin.name}"]`);

    if (pin.aliases?.includes('LED')) {
        // LED: toggle fill color
        el.style.fill = slot.value ? '#ffcc00' : '#333';
    }
    if (slot.role === HAL_ROLE_PWM) {
        // PWM: set opacity to duty cycle
        el.style.opacity = slot.value / 255;
    }
}
```

### NeoPixels

```python
import neopixel
pixels = neopixel.NeoPixel(board.NEOPIXEL, 10, brightness=0.2)
pixels[0] = (255, 0, 0)
pixels.show()
```

`neopixel_write` common-hal writes GRB color data to `/hal/neopixel` MEMFS
region.  JS reads the region after each frame and colors SVG circles:

```js
updateNeoPixels() {
    const data = memfs.readFile('/hal/neopixel');
    const pixelEls = this.svg.querySelectorAll('[data-component="NEOPIXEL"] circle');
    for (let i = 0; i < pixelEls.length; i++) {
        const g = data[i*3], r = data[i*3+1], b = data[i*3+2];
        pixelEls[i].style.fill = `rgb(${r},${g},${b})`;
    }
}
```

### Display (framebuffer)

`displayio` writes RGB565 pixels to the framebuffer in linear memory.
JS blits the framebuffer to a `<canvas>` element positioned inside the
SVG's `data-component="DISPLAY"` rect:

```js
updateDisplay() {
    if (!this.exports.display_dirty()) return;
    const fb = new Uint16Array(wasm.memory.buffer, fbAddr, width * height);
    // Convert RGB565 → RGBA and draw to canvas
    for (let i = 0; i < fb.length; i++) {
        const px = fb[i];
        imgData[i*4]   = (px >> 8 & 0xF8);  // R
        imgData[i*4+1] = (px >> 3 & 0xFC);  // G
        imgData[i*4+2] = (px << 3 & 0xF8);  // B
        imgData[i*4+3] = 255;
    }
    ctx.putImageData(imgData, 0, 0);
}
```

## The proxy/FFI layer's role

The jsffi proxy system is NOT exposed to the user in this model.  It's
used internally by common-hal and the port chassis for two purposes:

### 1. WASM imports: C tells JS things

These are low-level, port-internal:

```c
// "I claimed this pin, set up DOM listeners"
__attribute__((import_module("board"), import_name("registerPinListener")))
void port_register_pin_listener(uint32_t pin_id);

// "Pin visual state changed, update the SVG"
__attribute__((import_module("board"), import_name("updatePinVisual")))
void port_update_pin_visual(uint32_t pin_id, uint32_t role, uint32_t state);

// "Framebuffer is dirty, blit to canvas"
__attribute__((import_module("board"), import_name("requestDisplaySync")))
void port_request_display_sync(void);

// "I released this pin, tear down DOM listeners"
__attribute__((import_module("board"), import_name("releasePinListener")))
void port_release_pin_listener(uint32_t pin_id);
```

Common-hal calls these at construct/deinit time.  They're the equivalent
of writing to a GPIO controller's configuration registers.

### 2. Exported pointers: JS reads C state

JS reads MEMFS regions directly from linear memory.  No proxy objects,
no marshaling, no function calls.  The MEMFS `Uint8Array` view IS the
hardware register file.

### When proxies DO matter (future)

Two cases where the full proxy/FFI layer becomes relevant:

**A. External hardware (weBlinka)**

When a real I2C device is connected via WebUSB, `busio.I2C.writeto()`
must call `device.transferOut()` — a JS method returning a Promise.
Common-hal calls a WASM import that triggers the WebUSB transaction.
JS resolves the promise, writes the response to MEMFS, kicks the frame.
The VM resumes and reads the result.  No Python-level promise handling.

**B. Power-user escape hatch**

Advanced users who want direct browser access:
```python
from jsffi import global_this as js
js.console.log("hello from Python")
```
This uses the full proxy machinery (PVN marshaling, reference tables,
JsProxy type).  It's available but not the primary path.  Most users
never need it.

## Simulated components

### I2C device simulation

When a BME280 component is "connected" (via UI or configuration):

1. JS reads `components/bme280/definition.json`
2. JS creates a virtual I2C device at address 0x77
3. JS populates MEMFS with register values from the definition
4. When common-hal `busio.I2C.writeto_then_readfrom()` targets 0x77:
   - C writes the register address to `/hal/i2c/0/tx`
   - C reads the response from `/hal/i2c/0/rx`
   - JS intercepts: looks up the register in the component definition,
     returns the current simulated value
5. JS provides a UI widget (slider, gauge) to adjust sensor values
6. User drags temperature slider → JS updates register bytes → next
   Python read gets the new value

The Python code is identical to what runs on real hardware:
```python
import board, busio
from adafruit_bme280 import basic as adafruit_bme280
i2c = busio.I2C(board.SCL, board.SDA)
bme = adafruit_bme280.Adafruit_BME280_I2C(i2c)
print(bme.temperature)  # reads simulated register data
```

### Analog input simulation

Analog pins render as sliders in the board UI.  When the user drags
a slider for A0:

1. JS writes the 16-bit ADC value to `/hal/analog` MEMFS slot
2. JS pushes `SH_EVT_HW_CHANGE` with HAL_TYPE_ANALOG
3. Common-hal `analogio.AnalogIn.value` reads the MEMFS slot
4. Returns the slider value as a 16-bit integer (0-65535)

### PWM output visualization

When Python configures PWM on a pin:

```python
import pwmio
pwm = pwmio.PWMOut(board.D2, frequency=1000, duty_cycle=32768)
```

Common-hal writes frequency + duty to `/hal/pwm` MEMFS slot.
JS reads the slot after each frame and updates the visual:
- Pin pad pulses or shows a waveform indicator
- If the pin is connected to a servo component, the servo arm rotates
- If connected to an LED, brightness tracks duty cycle

## Packet protocol at the frame boundary

Each `chassis_frame()` call is a structured exchange.  Rather than loose
MEMFS reads and scattered state flags, the frame boundary is a packet:

### Inbound packet (JS → C)

JS assembles this before calling `chassis_frame()`:

```
┌──────────────────────────────┐
│ header (8 bytes)             │
│   frame_count: u32           │
│   flags: u32                 │
│     bit 0: serial_available  │
│     bit 1: events_available  │
│     bit 2: ctrl_c            │
│     bit 3: ctrl_d            │
│     bit 4: file_changed      │
├──────────────────────────────┤
│ serial data (variable)       │
│   length: u16                │
│   bytes: u8[length]          │
├──────────────────────────────┤
│ events already in event ring │
│   (JS wrote them before call)│
└──────────────────────────────┘
```

Most data is already in MEMFS (event ring, serial rx buffer).  The
packet header is metadata about what changed, not the data itself.

### Outbound packet (C → JS)

C writes this before returning from `chassis_frame()`:

```
┌──────────────────────────────┐
│ header (8 bytes)             │
│   status: u8 (DONE/YIELD/   │
│           SLEEPING/ERROR)    │
│   flags: u8                  │
│     bit 0: serial_output     │
│     bit 1: display_dirty     │
│     bit 2: neopixel_dirty    │
│     bit 3: gpio_dirty        │
│   wakeup_ms: u32             │
│   reserved: u16              │
├──────────────────────────────┤
│ dirty bitmasks               │
│   gpio_dirty: u64            │
│   analog_dirty: u64          │
│   pwm_dirty: u64             │
├──────────────────────────────┤
│ serial output already in tx  │
│   (C wrote to /hal/serial/tx)│
└──────────────────────────────┘
```

JS reads the header, checks dirty bits, and updates only what changed.

### Why packets

1. **Inspectable**: log every frame exchange for debugging
2. **Versioned**: header includes a version byte for forward compat
3. **Efficient**: dirty bitmasks avoid scanning all 64 pins every frame
4. **Clean boundary**: one call, one response, no interleaved state

## Board definition as the single source of truth

The definition.json drives everything:

| Consumer | What it reads | Purpose |
|----------|---------------|---------|
| JS renderer | pins, visual, power | Render SVG, position labels |
| JS listeners | pins.capabilities | Choose interaction mode (click vs slider vs toggle) |
| MEMFS init | pins.id, capabilities | Populate template slots with categories |
| C board_pins.c | pins.name, pins.id | `board.D5` → GPIO 5 mapping |
| C hal_init | pins.capabilities | Set HAL_CAT_* per pin |
| Component sim | components/*.json | Virtual I2C/SPI devices |

Today, `board_pins.c` is hand-written C that must match definition.json.
Future: generate `board_pins.c` from definition.json at build time.
The definition is the source; C code is derived.

## Relationship to displayio

The built-in display follows the same pattern as on real hardware:

1. `board_display_init()` creates a `FramebufferDisplay` backed by a
   pixel buffer in linear memory
2. The supervisor's terminal writes REPL output to this display via
   `terminalio` — same code path as on a Feather with built-in screen
3. JS positions a `<canvas>` over the SVG's `data-component="DISPLAY"`
   rect and blits the framebuffer after each frame

The display is just another MEMFS-backed peripheral.  The user sees
`board.DISPLAY` and uses it like any other CircuitPython display.

## What stays the same

The entire point is that nothing changes for the user:

- `import board` — same
- `digitalio.DigitalInOut(board.D5)` — same
- `pin.value` — same
- `neopixel.NeoPixel(board.NEOPIXEL, 10)` — same
- `board.DISPLAY` — same
- `busio.I2C(board.SCL, board.SDA)` — same
- `adafruit_bme280.Adafruit_BME280_I2C(i2c)` — same

The common-hal layer is different (MEMFS instead of hardware registers),
but that's what common-hal IS — the port-specific implementation behind
a stable API.

## What the proxy/FFI layer enables (internally)

- **WASM imports**: common-hal tells JS when pins are claimed/released
- **MEMFS views**: JS reads hardware state without function calls
- **Event ring**: JS notifies C of user interactions
- **Dirty flags**: C tells JS what changed without JS polling everything

The proxy layer (PVN marshaling, reference tables, JsProxy/PyProxy types)
is available for future use (external hardware via WebUSB, `import js`
escape hatch) but is not on the critical path for board-as-UI.

## Implementation sequence

### Phase 1: SVG rendering from definition.json

JS reads definition.json, renders board.svg into the page, positions
pin labels and indicators.  No C interaction yet — pure JS.

### Phase 2: Pin interaction (input)

Wire pointerdown/pointerup on pin elements to MEMFS writes + event ring.
Verify: click a pin in the SVG, see the value change in MEMFS, Python
reads it via common-hal.

### Phase 3: Pin visualization (output)

After each frame, JS reads dirty flags and updates SVG elements:
LED fill, PWM opacity, NeoPixel colors.  Verify: Python sets LED on,
SVG circle lights up.

### Phase 4: Display integration

Position a `<canvas>` inside the SVG display area.  Blit framebuffer
after each frame.  Verify: REPL text appears on the in-board display.

### Phase 5: Analog input widgets

Render sliders for analog-capable pins.  Slider changes write to
`/hal/analog` MEMFS region.  Verify: drag slider, `analogio.AnalogIn`
reads the value.

### Phase 6: Component simulation

Load component definitions, create virtual I2C/SPI devices, render
sensor widgets.  Verify: connect a BME280, drag temperature slider,
Python reads the simulated temperature.

### Phase 7: Build-time board generation

Generate `board_pins.c` and `mpconfigboard.h` from definition.json.
One definition file, zero hand-maintained C tables.

## Open questions

1. **SVG authoring tooling**: Who creates board.svg files?  Can we
   provide a template or tool that generates a basic SVG from
   definition.json pin coordinates?

2. **Multiple boards**: Can the same WASM binary support different
   definition.json files (different pin layouts, different components)?
   Answer: yes, if board_pins.c is the superset and definition.json
   selects which pins are visible.  Or: generate per-board builds.

3. **Touch/gesture input**: How do touch-capable pins simulate
   capacitive touch?  Hover = proximity?  Click = touch?  Drag =
   swipe?

4. **Audio output**: `audiopwmio` or `audiobusio` — how do we render
   audio?  Web Audio API is the obvious answer, but the common-hal
   boundary needs design.

5. **Real hardware alongside simulation**: Can some pins be simulated
   (buttons, LEDs) while others route to real hardware via WebUSB?
   The MEMFS model supports this: simulated pins use MEMFS, real pins
   use the weBlinka USB bridge.  Both write to the same MEMFS slots.
