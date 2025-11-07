# CircuitPython WASM Peripheral Architecture

## Overview

The CircuitPython WASM port uses a **layered architecture** that separates the core runtime from environment-specific peripheral implementations. This design enables the same core API to run in browsers, Node.js, and custom JavaScript environments.

## Architecture Layers

```
┌─────────────────────────────────────────────────────┐
│ Layer 3: Application                                │
│ - User code (code.py, boot.py)                     │
│ - Custom peripheral implementations                 │
└─────────────────────────────────────────────────────┘
                        ↕
┌─────────────────────────────────────────────────────┐
│ Layer 2: Environment Peripherals (Optional)         │
│ - DisplayController, I2CController, etc.            │
│ - Browser: Canvas-based display, Web Serial API    │
│ - Node.js: Terminal display, native serial         │
│ - Custom: Application-specific implementations     │
└─────────────────────────────────────────────────────┘
                        ↕
┌─────────────────────────────────────────────────────┐
│ Layer 1: Core Runtime (circuitpython.mjs)          │
│ - WebAssembly binary                                │
│ - Core API (api.js, objpyproxy.js, etc.)          │
│ - State arrays (GPIO, analog, PWM)                 │
│ - Peripheral hook system                            │
│ - Environment-agnostic                              │
└─────────────────────────────────────────────────────┘
```

## Module Types

### Type A: Self-Contained Modules
These modules work entirely within WASM memory and require no external interaction:

- **digitalio**: GPIO pins, read/write digital values
- **analogio**: Analog input/output
- **pwmio**: PWM output
- **rotaryio**: Rotary encoder simulation
- **neopixel_write**: NeoPixel bit patterns
- **microcontroller**: Microcontroller info
- **board**: Board pin definitions
- **time**: Time functions (using JavaScript Date)

**Implementation**: State arrays in C, no peripheral hooks needed.

### Type B: External-Interaction Modules
These modules require interaction with the external environment:

- **busio.I2C**: I2C bus communication (needs device simulation)
- **busio.SPI**: SPI bus communication (needs device simulation)
- **busio.UART**: Serial communication (needs terminal or serial port)
- **displayio**: Display rendering (needs canvas or terminal)
- **storage**: Persistent storage (needs filesystem backend)
- **usb_cdc**: USB serial (needs serial interface)

**Implementation**: Peripheral hooks that applications provide.

## Peripheral Hook System

### Core API Functions

The core API provides these functions for managing peripherals:

```javascript
const ctpy = await loadCircuitPython({ verbose: true });

// Attach a peripheral implementation
ctpy.peripherals.attach(name, implementation);

// Get a peripheral implementation
const display = ctpy.peripherals.get('display');

// Check if a peripheral is attached
if (ctpy.peripherals.has('display')) { ... }

// Detach a peripheral
ctpy.peripherals.detach('display');

// List all attached peripherals
const names = ctpy.peripherals.list();
```

### Accessing from C Code

C code can check for peripherals using EM_JS macros:

```c
EM_JS(bool, has_display_peripheral, (), {
    return Module.hasPeripheral('display');
});

EM_JS(void, display_show, (uint8_t *buffer, int width, int height), {
    const peripheral = Module.getPeripheral('display');
    if (peripheral && peripheral.show) {
        const data = new Uint8Array(Module.HEAPU8.buffer, buffer, width * height * 4);
        peripheral.show(data, width, height);
    }
});
```

## Example Implementations

### Example 1: Display Peripheral (Browser)

```javascript
// Create a canvas-based display peripheral
class CanvasDisplayController {
    constructor(canvas) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
    }

    show(data, width, height) {
        // Render RGBA data to canvas
        const imageData = new ImageData(
            new Uint8ClampedArray(data),
            width,
            height
        );
        this.ctx.putImageData(imageData, 0, 0);
    }

    clear() {
        this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    }
}

// Attach to CircuitPython
const canvas = document.getElementById('display');
const displayController = new CanvasDisplayController(canvas);
ctpy.peripherals.attach('display', displayController);
```

### Example 2: I2C Peripheral with Device Simulation

```javascript
// Simulated I2C device (e.g., temperature sensor)
class SimulatedTempSensor {
    constructor(address) {
        this.address = address;
        this.temperature = 25.0; // Start at 25°C
    }

    read(register, length) {
        if (register === 0x00 && length === 2) {
            // Return temperature as 16-bit value
            const temp = Math.round(this.temperature * 100);
            return new Uint8Array([temp >> 8, temp & 0xFF]);
        }
        return new Uint8Array(length);
    }

    write(register, data) {
        // Handle writes if needed
    }
}

// I2C bus controller
class I2CController {
    constructor() {
        this.devices = new Map();
    }

    attachDevice(address, device) {
        this.devices.set(address, device);
    }

    probe(address) {
        return this.devices.has(address);
    }

    read(address, register, length) {
        const device = this.devices.get(address);
        return device ? device.read(register, length) : null;
    }

    write(address, register, data) {
        const device = this.devices.get(address);
        if (device) device.write(register, data);
    }
}

// Setup
const i2c = new I2CController();
i2c.attachDevice(0x48, new SimulatedTempSensor(0x48));
ctpy.peripherals.attach('i2c', i2c);
```

### Example 3: Serial Peripheral (Node.js)

```javascript
import { SerialPort } from 'serialport';

class NodeSerialController {
    constructor(portPath) {
        this.port = new SerialPort({ path: portPath, baudRate: 115200 });
        this.dataCallback = null;
    }

    write(data) {
        this.port.write(data);
    }

    onData(callback) {
        this.dataCallback = callback;
        this.port.on('data', callback);
    }

    close() {
        this.port.close();
    }
}

// Attach
const serial = new NodeSerialController('/dev/ttyUSB0');
ctpy.peripherals.attach('serial', serial);
```

## Implementation Guidelines

### For Core API Developers

1. **Keep core environment-agnostic**: Don't assume browser or Node.js APIs
2. **Use peripheral hooks for external interaction**: Check `Module.hasPeripheral()` before calling
3. **Provide fallbacks**: Gracefully handle missing peripherals
4. **Document expected interface**: Clearly define what methods peripherals should implement

### For Application Developers

1. **Attach peripherals before running code**: Ensure peripherals are ready before `runWorkflow()`
2. **Implement expected interfaces**: Match the interface documented for each peripheral type
3. **Handle errors gracefully**: Peripheral methods may be called frequently
4. **Clean up resources**: Use `detachPeripheral()` when done

### For Peripheral Implementers

1. **Match the documented interface**: Follow conventions for method names and signatures
2. **Be synchronous when possible**: Avoid async unless necessary
3. **Cache state locally**: Don't rely on WASM state arrays directly
4. **Emit events for changes**: Allow applications to observe peripheral state

## Standard Peripheral Interfaces

### Display Peripheral
```typescript
interface DisplayPeripheral {
    show(data: Uint8Array, width: number, height: number): void;
    clear(): void;
    setBrightness?(brightness: number): void;
}
```

### I2C Peripheral
```typescript
interface I2CPeripheral {
    probe(address: number): boolean;
    read(address: number, register: number, length: number): Uint8Array | null;
    write(address: number, register: number, data: Uint8Array): void;
    attachDevice?(address: number, device: I2CDevice): void;
}
```

### Serial Peripheral
```typescript
interface SerialPeripheral {
    write(data: Uint8Array): void;
    read(length: number): Uint8Array | null;
    onData(callback: (data: Uint8Array) => void): void;
    available(): number;
}
```

### SPI Peripheral
```typescript
interface SPIPeripheral {
    transfer(data: Uint8Array): Uint8Array;
    configure(options: { frequency: number, mode: number }): void;
}
```

## Benefits of This Architecture

1. **Environment Independence**: Core API works anywhere JavaScript runs
2. **Flexibility**: Applications choose which peripherals to implement
3. **Performance**: No unnecessary abstractions in the core
4. **Testing**: Easy to mock peripherals for testing
5. **Progressive Enhancement**: Start simple, add peripherals as needed

## Migration Path

For existing applications using embedded controllers:

1. Extract controller classes from core API
2. Create peripheral implementations
3. Attach peripherals in application initialization
4. Update C code to use peripheral hooks instead of direct calls

Example:

```javascript
// Before (controllers in core)
const ctpy = await loadCircuitPython();
// Controllers automatically available

// After (layered architecture)
const ctpy = await loadCircuitPython();
const display = new CanvasDisplayController(document.getElementById('display'));
ctpy.peripherals.attach('display', display);
```

## Next Steps

1. **Implement displayio common-hal**: Use display peripheral hook in C code
2. **Refactor busio**: Use I2C/SPI peripheral hooks
3. **Create peripheral library**: Standard implementations for browser/Node.js
4. **Update documentation**: Document each peripheral interface
5. **Create examples**: Show real-world usage patterns
