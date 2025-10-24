# JavaScript ↔ Virtual Hardware Interface

## Overview

The CircuitPython WASM port implements a **bidirectional interface** between JavaScript and the internal virtual hardware. This allows JavaScript to interact with the WASM runtime exactly like the physical world interacts with a real CircuitPython board.

## Architecture

```
┌────────────────────────────────────────────────────────┐
│                    JavaScript Layer                    │
│  (External World - Simulates physical interactions)   │
│                                                        │
│  • Observe outputs (LED states, DAC values)           │
│  • Inject inputs (button presses, sensor readings)    │
│  • Visualize hardware state in browser UI             │
└──────────────────┬─────────────────────────────────────┘
                   │
          Exported C Functions
          (see list below)
                   │
┌──────────────────▼─────────────────────────────────────┐
│              CircuitPython WASM Runtime                │
│                                                        │
│  ┌──────────────────────────────────────────────────┐ │
│  │  Python Code                                     │ │
│  │  import digitalio                                │ │
│  │  led.value = True                                │ │
│  │  button_pressed = not button.value               │ │
│  └────────────────┬─────────────────────────────────┘ │
│                   │                                    │
│  ┌────────────────▼─────────────────────────────────┐ │
│  │  common-hal/digitalio/*.c                        │ │
│  │  Synchronous calls - no yielding!                │ │
│  └────────────────┬─────────────────────────────────┘ │
│                   │                                    │
│  ┌────────────────▼─────────────────────────────────┐ │
│  │  virtual_hardware.c                              │ │
│  │                                                  │ │
│  │  GPIO State (64 pins):                          │ │
│  │    - value (bool)                               │ │
│  │    - direction (input/output)                   │ │
│  │    - pull (none/up/down)                        │ │
│  │                                                  │ │
│  │  Analog State (64 pins):                        │ │
│  │    - value (16-bit)                             │ │
│  │    - is_output (ADC/DAC)                        │ │
│  └──────────────────────────────────────────────────┘ │
│                                                        │
└────────────────────────────────────────────────────────┘
```

## Key Principle

**The WASM runtime runs self-sufficiently.** JavaScript doesn't "control" the hardware - it observes outputs and provides inputs, just like the physical world does with a real board.

- **Python sets output → JavaScript reads it**
- **JavaScript sets input → Python reads it**
- **No waiting, no yielding, all instant!**

## JavaScript API Reference

All functions are accessible on the object returned by `loadCircuitPython()`.

### GPIO Output Observation

#### `_virtual_gpio_get_output_value(pin: number): boolean`

Read the current output value of a GPIO pin.

```javascript
const ledState = ctpy._virtual_gpio_get_output_value(13);
console.log(`LED is ${ledState ? 'ON' : 'OFF'}`);
```

**Use cases:**
- Visualizing LED states in browser UI
- Logging output changes
- Syncing with physical LED hardware (via Web Serial/WebUSB)

#### `_virtual_gpio_get_direction(pin: number): number`

Get the pin's configured direction.

```javascript
const direction = ctpy._virtual_gpio_get_direction(13);
// 0 = INPUT, 1 = OUTPUT
```

#### `_virtual_gpio_get_pull(pin: number): number`

Get the pin's pull resistor configuration.

```javascript
const pull = ctpy._virtual_gpio_get_pull(2);
// 0 = NONE, 1 = PULL_UP, 2 = PULL_DOWN
```

### GPIO Input Simulation

#### `_virtual_gpio_set_input_value(pin: number, value: boolean): void`

Simulate an external input signal (e.g., button press, sensor trigger).

```javascript
// Simulate button press (active-low)
ctpy._virtual_gpio_set_input_value(2, false);

// Wait for Python to process
setTimeout(() => {
    // Simulate button release
    ctpy._virtual_gpio_set_input_value(2, true);
}, 500);
```

**Important:** Only works on pins configured as INPUT in Python. The hardware respects pin configuration!

### Analog Output Observation

#### `_virtual_analog_get_output_value(pin: number): number`

Read the current DAC output value (0-65535).

```javascript
const dacValue = ctpy._virtual_analog_get_output_value(A0);
console.log(`DAC output: ${dacValue} (${(dacValue/65535*3.3).toFixed(2)}V)`);
```

#### `_virtual_analog_is_enabled(pin: number): boolean`

Check if analog functionality is enabled on the pin.

#### `_virtual_analog_is_output(pin: number): boolean`

Check if the analog pin is configured as DAC (true) or ADC (false).

### Analog Input Simulation

#### `_virtual_analog_set_input_value(pin: number, value: number): void`

Simulate an analog sensor reading (ADC input, 0-65535).

```javascript
// Simulate a temperature sensor reading 2.5V (mid-range)
const adcValue = Math.floor((2.5 / 3.3) * 65535);
ctpy._virtual_analog_set_input_value(A1, adcValue);
```

**Important:** Only works on pins configured as ADC (AnalogIn) in Python.

## Example Use Cases

### 1. LED Visualization in Browser

```html
<div id="led13" class="led"></div>

<script type="module">
import loadCircuitPython from './circuitpython.mjs';

const ctpy = await loadCircuitPython({ ... });

// Write Python code that blinks LED
ctpy.FS.writeFile('/code.py', `
import board
import digitalio
import time

led = digitalio.DigitalInOut(board.D13)
led.direction = digitalio.Direction.OUTPUT

while True:
    led.value = not led.value
    time.sleep(0.5)
`);

// Visualize LED state in browser
setInterval(() => {
    const ledState = ctpy._virtual_gpio_get_output_value(13);
    document.getElementById('led13').classList.toggle('on', ledState);
}, 100);
</script>
```

### 2. Button Simulation

```html
<button id="btn">Press Button D2</button>

<script type="module">
const button = document.getElementById('btn');

button.addEventListener('mousedown', () => {
    // Active-low button
    ctpy._virtual_gpio_set_input_value(2, false);
});

button.addEventListener('mouseup', () => {
    ctpy._virtual_gpio_set_input_value(2, true);
});
</script>
```

### 3. Sensor Data Injection

```javascript
// Simulate a temperature sensor that varies over time
function simulateTemperatureSensor() {
    setInterval(() => {
        // Temperature range: 20-30°C mapped to 0-3.3V
        const temp = 20 + Math.random() * 10;
        const voltage = (temp - 20) / 10 * 3.3;
        const adcValue = Math.floor(voltage / 3.3 * 65535);

        ctpy._virtual_analog_set_input_value(A0, adcValue);
    }, 1000);
}
```

### 4. Hardware State Dashboard

```javascript
function createHardwareDashboard() {
    setInterval(() => {
        const state = {
            gpio: [],
            analog: []
        };

        // Read all 64 GPIO pins
        for (let pin = 0; pin < 64; pin++) {
            const direction = ctpy._virtual_gpio_get_direction(pin);
            if (direction === 1) { // Output
                state.gpio.push({
                    pin,
                    type: 'output',
                    value: ctpy._virtual_gpio_get_output_value(pin)
                });
            }
        }

        // Read all analog pins
        for (let pin = 0; pin < 64; pin++) {
            if (ctpy._virtual_analog_is_enabled(pin)) {
                const isOutput = ctpy._virtual_analog_is_output(pin);
                state.analog.push({
                    pin,
                    type: isOutput ? 'DAC' : 'ADC',
                    value: isOutput ?
                        ctpy._virtual_analog_get_output_value(pin) :
                        null // Can't read ADC values from JS
                });
            }
        }

        updateDashboard(state);
    }, 100);
}
```

## Important Notes

### 1. Respects Pin Configuration

The virtual hardware enforces proper pin modes:

```javascript
// Python configures pin as OUTPUT
ctpy.runPython(`
    led = digitalio.DigitalInOut(board.D13)
    led.direction = digitalio.Direction.OUTPUT
`);

// ✅ JavaScript can READ output
const value = ctpy._virtual_gpio_get_output_value(13);

// ❌ JavaScript CANNOT set input (pin is OUTPUT)
ctpy._virtual_gpio_set_input_value(13, true); // Silently ignored!
```

### 2. No Callbacks or Events

This interface is **polling-based**. JavaScript must check hardware state in a loop or timer.

**Why?** The WASM runtime runs synchronously without event loops. JavaScript observes state changes after they happen.

**Future enhancement:** Could add a change notification system using shared memory or message passing.

### 3. Performance

- Reading pin states: **< 1μs** (direct memory access)
- Setting pin values: **< 1μs** (direct memory write)
- No context switches or yields!

Safe to poll at 60 FPS (every 16ms) for smooth visualization.

### 4. No Direct VFS/Module Access

These functions only work on the hardware layer. To run Python code or access files, use:

- `ctpy.runPython(code)` - Execute Python code
- `ctpy.FS.writeFile(path, content)` - Write to virtual filesystem
- `ctpy.pyimport(module)` - Import Python module

## Comparison with Message Queue (Old Approach)

| Aspect | Message Queue (Old) | Virtual Hardware (New) |
|--------|-------------------|----------------------|
| **GPIO Write** | Yields to JS, waits for response | Instant C function call |
| **GPIO Read** | Yields to JS, waits for response | Instant C function call |
| **Hanging in CLI** | Yes (no JS handler) | No (self-contained) |
| **Speed** | ~100-200μs | < 1μs |
| **JS Interaction** | Required for operation | Optional for simulation |
| **Architecture** | Hardware in JS layer | Hardware in C layer |

## Demo Files

- [`demo_hardware_interaction.html`](demo_hardware_interaction.html) - Interactive browser demo
- [`test_hw_api.mjs`](test_hw_api.mjs) - Node.js API validation test
- [`test_hw_simple.mjs`](test_hw_simple.mjs) - Simple interaction example

## See Also

- [VIRTUAL_HARDWARE.md](VIRTUAL_HARDWARE.md) - Internal C implementation details
- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - Overall architecture
- [API Reference (api.js)](api.js) - Complete JavaScript API

---

**Key Takeaway**: JavaScript interacts with CircuitPython WASM the same way the physical world interacts with a real board - by providing inputs and observing outputs. The runtime is self-sufficient and doesn't depend on JavaScript for basic operation!
