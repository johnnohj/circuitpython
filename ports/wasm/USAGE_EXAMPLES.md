# CircuitPython WASM Usage Examples

This document shows practical examples of using the CircuitPython WASM port with different architectural approaches.

## Table of Contents
1. [Minimal Core API Usage (Layer 1)](#minimal-core-api-usage-layer-1)
2. [Batteries-Included Usage (Layer 2)](#batteries-included-usage-layer-2)
3. [Custom Peripheral Implementation](#custom-peripheral-implementation)
4. [Node.js Usage](#nodejs-usage)

---

## Minimal Core API Usage (Layer 1)

Use this approach when you want maximum control and minimal overhead.

### Basic Setup

```javascript
import { loadCircuitPython } from './circuitpython.mjs';

// Load the core runtime
const ctpy = await loadCircuitPython({
    heapsize: 128 * 1024,
    verbose: true,
    filesystem: 'memory'  // or 'indexeddb'
});

// Run Python code
ctpy.runPython(`
import digitalio
import board

# Create an LED pin
led = digitalio.DigitalInOut(board.D13)
led.direction = digitalio.Direction.OUTPUT
led.value = True
`);

// Check GPIO state from JavaScript
const ledState = ctpy._module._virtual_gpio_get_output_value(13);
console.log('LED is', ledState ? 'ON' : 'OFF');
```

### With Custom Display Peripheral

```javascript
import { loadCircuitPython } from './circuitpython.mjs';

// Create a simple display peripheral
const displayPeripheral = {
    show(data, width, height) {
        console.log(`Display updated: ${width}x${height}, ${data.length} bytes`);
        // Your custom rendering logic here
    },
    clear() {
        console.log('Display cleared');
    }
};

// Load runtime and attach peripheral
const ctpy = await loadCircuitPython({ verbose: true });
ctpy.peripherals.attach('display', displayPeripheral);

// Now displayio module can use it
ctpy.runPython(`
import displayio

# This will call displayPeripheral.show() when refresh() is called
display = displayio.Display()
# ... create display groups, shapes, etc ...
display.refresh()
`);
```

### Monitoring Hardware Changes

```javascript
const ctpy = await loadCircuitPython();

// Monitor GPIO changes by polling state arrays
setInterval(() => {
    const allPins = Array.from({ length: 32 }, (_, i) => ({
        pin: i,
        value: ctpy._module._virtual_gpio_get_output_value(i),
        direction: ctpy._module._virtual_gpio_get_direction(i)
    })).filter(p => p.direction !== 0);  // 0 = not configured

    console.table(allPins);
}, 1000);

// Run code that manipulates pins
ctpy.runWorkflow();
```

---

## Batteries-Included Usage (Layer 2)

Use this approach for a complete board experience with all peripherals pre-configured.

### Browser Example with Canvas Display

```html
<!DOCTYPE html>
<html>
<head>
    <title>CircuitPython WASM Board</title>
</head>
<body>
    <canvas id="display" width="320" height="240"></canvas>
    <pre id="terminal"></pre>

    <script type="module">
        import { CircuitPythonBoard } from './src/core/board.js';

        // Create a complete board instance
        const board = new CircuitPythonBoard({
            canvasId: 'display',
            storagePrefix: 'my-circuitpy-app',
            heapSize: 256 * 1024,
            verbose: true,
            filesystem: 'indexeddb'
        });

        // Connect and initialize everything
        await board.connect();

        // Hook up serial to terminal
        const terminal = document.getElementById('terminal');
        board.serial.onData((data) => {
            terminal.textContent += data;
        });

        // Upload code.py
        await board.storage.writeFile('/code.py', `
import time
import board
import digitalio
import displayio

# Setup LED
led = digitalio.DigitalInOut(board.D13)
led.direction = digitalio.Direction.OUTPUT

# Setup display
display = displayio.Display()
# ... create display content ...

while True:
    led.value = not led.value
    display.refresh()
    time.sleep(0.5)
`);

        // Run the workflow (boot.py then code.py)
        board.workflow.start();

        // Monitor board status
        setInterval(() => {
            const status = board.getStatus();
            console.log('Board status:', status);
        }, 5000);

        // Access hardware directly
        const pin13 = board.getPin(13);
        console.log('Pin 13:', {
            direction: pin13.direction,
            value: pin13.value
        });

        // Clean up on page unload
        window.addEventListener('beforeunload', () => {
            board.disconnect();
        });
    </script>
</body>
</html>
```

### Interactive REPL Example

```html
<!DOCTYPE html>
<html>
<head>
    <title>CircuitPython REPL</title>
    <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/xterm/css/xterm.css" />
    <script src="https://cdn.jsdelivr.net/npm/xterm/lib/xterm.js"></script>
</head>
<body>
    <div id="terminal-container"></div>

    <script type="module">
        import { CircuitPythonBoard } from './src/core/board.js';

        // Create terminal
        const term = new Terminal({
            cursorBlink: true,
            fontSize: 14,
            theme: {
                background: '#000000',
                foreground: '#ffffff'
            }
        });
        term.open(document.getElementById('terminal-container'));

        // Create board
        const board = new CircuitPythonBoard({ verbose: false });
        await board.connect();

        // Connect serial to terminal
        board.serial.onData((data) => term.write(data));
        term.onData((data) => board.serial.write(data));

        // Initialize REPL
        board.module.replInit();

        // Handle terminal input
        term.onKey(({ key, domEvent }) => {
            const charCode = key.charCodeAt(0);

            // Handle Ctrl+C
            if (domEvent.ctrlKey && domEvent.key === 'c') {
                board.module.replProcessChar(0x03);
                return;
            }

            // Handle Ctrl+D (soft reset)
            if (domEvent.ctrlKey && domEvent.key === 'd') {
                board.reset();
                return;
            }

            // Send character to REPL
            board.module.replProcessChar(charCode);
        });

        term.writeln('CircuitPython WASM REPL');
        term.writeln('Press Ctrl+C to enter REPL, Ctrl+D to soft reset');
        term.writeln('');
    </script>
</body>
</html>
```

---

## Custom Peripheral Implementation

Example of creating a custom peripheral for a specific device simulation.

### Simulated Temperature Sensor via I2C

```javascript
import { loadCircuitPython } from './circuitpython.mjs';

// Custom I2C device simulation
class BME280Simulator {
    constructor() {
        this.temperature = 25.0;  // °C
        this.pressure = 1013.25;  // hPa
        this.humidity = 50.0;     // %
    }

    read(register, length) {
        // BME280 register map
        if (register === 0xFA) {  // pressure_msb
            const raw = Math.round(this.pressure * 100);
            return new Uint8Array([
                (raw >> 16) & 0xFF,
                (raw >> 8) & 0xFF,
                raw & 0xFF
            ]);
        } else if (register === 0xFD) {  // temp_msb
            const raw = Math.round((this.temperature + 50) * 100);
            return new Uint8Array([
                (raw >> 16) & 0xFF,
                (raw >> 8) & 0xFF,
                raw & 0xFF
            ]);
        } else if (register === 0xFD) {  // hum_msb
            const raw = Math.round(this.humidity * 100);
            return new Uint8Array([
                (raw >> 8) & 0xFF,
                raw & 0xFF
            ]);
        }
        return new Uint8Array(length);
    }

    write(register, data) {
        if (register === 0xF4) {  // ctrl_meas
            console.log('BME280: Starting measurement');
        }
    }

    // Simulate environmental changes
    update() {
        this.temperature += (Math.random() - 0.5) * 0.1;
        this.pressure += (Math.random() - 0.5) * 0.5;
        this.humidity += (Math.random() - 0.5) * 1.0;
    }
}

// I2C peripheral that manages devices
class I2CPeripheral {
    constructor() {
        this.devices = new Map();
    }

    attachDevice(address, device) {
        this.devices.set(address, device);
        console.log(`I2C: Attached device at 0x${address.toString(16)}`);
    }

    probe(address) {
        return this.devices.has(address);
    }

    read(address, register, length) {
        const device = this.devices.get(address);
        if (!device) {
            throw new Error(`No device at I2C address 0x${address.toString(16)}`);
        }
        return device.read(register, length);
    }

    write(address, register, data) {
        const device = this.devices.get(address);
        if (!device) {
            throw new Error(`No device at I2C address 0x${address.toString(16)}`);
        }
        device.write(register, data);
    }
}

// Setup
const ctpy = await loadCircuitPython({ verbose: true });

const i2c = new I2CPeripheral();
const bme280 = new BME280Simulator();
i2c.attachDevice(0x76, bme280);

ctpy.peripherals.attach('i2c', i2c);

// Simulate environmental changes
setInterval(() => bme280.update(), 1000);

// Run Python code that reads the sensor
await ctpy.saveFile('/code.py', `
import time
import board
from adafruit_bme280 import basic as adafruit_bme280

i2c = board.I2C()
bme = adafruit_bme280.Adafruit_BME280_I2C(i2c)

while True:
    print(f"Temperature: {bme.temperature:.1f}°C")
    print(f"Humidity: {bme.humidity:.1f}%")
    print(f"Pressure: {bme.pressure:.2f}hPa")
    print()
    time.sleep(1)
`);

ctpy.runWorkflow();
```

---

## Node.js Usage

Running CircuitPython in Node.js for automated testing or CLI tools.

### Basic CLI Script

```javascript
#!/usr/bin/env node
import { loadCircuitPython } from './circuitpython.mjs';

async function main() {
    // Load with stdout/stderr redirected
    const ctpy = await loadCircuitPython({
        heapsize: 128 * 1024,
        stdout: (data) => process.stdout.write(data),
        stderr: (data) => process.stderr.write(data),
        linebuffer: false,
        verbose: false
    });

    // Read script from file or argument
    const scriptPath = process.argv[2] || 'code.py';
    const code = await fs.promises.readFile(scriptPath, 'utf-8');

    try {
        ctpy.runPython(code);
        process.exit(0);
    } catch (error) {
        if (error.type === 'SystemExit') {
            process.exit(error.args[0] || 0);
        }
        console.error(error.message);
        process.exit(1);
    }
}

main();
```

### Testing Framework

```javascript
import { loadCircuitPython } from './circuitpython.mjs';
import assert from 'node:assert';

class TestHarness {
    constructor() {
        this.tests = [];
        this.results = [];
    }

    test(name, fn) {
        this.tests.push({ name, fn });
    }

    async run() {
        console.log(`Running ${this.tests.length} tests...\n`);

        for (const { name, fn } of this.tests) {
            try {
                await fn();
                this.results.push({ name, passed: true });
                console.log(`✓ ${name}`);
            } catch (error) {
                this.results.push({ name, passed: false, error });
                console.log(`✗ ${name}`);
                console.log(`  ${error.message}`);
            }
        }

        const passed = this.results.filter(r => r.passed).length;
        const failed = this.results.length - passed;

        console.log(`\n${passed} passed, ${failed} failed`);
        return failed === 0;
    }
}

// Create test suite
const tests = new TestHarness();

tests.test('Digital GPIO output', async () => {
    const ctpy = await loadCircuitPython({ verbose: false });

    ctpy.runPython(`
import digitalio
import board

led = digitalio.DigitalInOut(board.D13)
led.direction = digitalio.Direction.OUTPUT
led.value = True
    `);

    const value = ctpy._module._virtual_gpio_get_output_value(13);
    assert.strictEqual(value, 1, 'LED should be on');
});

tests.test('Analog input', async () => {
    const ctpy = await loadCircuitPython({ verbose: false });

    // Set analog input value from JS
    ctpy._module._virtual_analog_set_input_value(0, 32768);  // Mid-range

    const result = ctpy.runPython(`
import analogio
import board

analog = analogio.AnalogIn(board.A0)
analog.value
    `);

    assert.ok(result >= 30000 && result <= 35000, 'Analog value in expected range');
});

tests.test('PWM output', async () => {
    const ctpy = await loadCircuitPython({ verbose: false });

    ctpy.runPython(`
import pwmio
import board

pwm = pwmio.PWMOut(board.D9, frequency=1000, duty_cycle=32768)
    `);

    // Check PWM state in C arrays
    const value = ctpy._module._virtual_pwm_get_duty_cycle(9);
    assert.strictEqual(value, 32768, 'PWM duty cycle should be 50%');
});

// Run tests
const success = await tests.run();
process.exit(success ? 0 : 1);
```

### Node.js with Real Serial Port

```javascript
import { SerialPort } from 'serialport';
import { loadCircuitPython } from './circuitpython.mjs';

// Custom serial peripheral using real hardware
class NodeSerialPeripheral {
    constructor(path, baudRate = 115200) {
        this.port = new SerialPort({ path, baudRate });
        this.dataCallbacks = [];

        this.port.on('data', (data) => {
            for (const cb of this.dataCallbacks) {
                cb(data);
            }
        });
    }

    write(data) {
        this.port.write(Buffer.from(data));
    }

    onData(callback) {
        this.dataCallbacks.push(callback);
    }

    available() {
        return this.port.readable ? 1 : 0;
    }

    close() {
        this.port.close();
    }
}

// Setup
const ctpy = await loadCircuitPython({
    stdout: (data) => process.stdout.write(data),
    verbose: true
});

const serial = new NodeSerialPeripheral('/dev/ttyUSB0');
ctpy.peripherals.attach('serial', serial);

// Now Python code can use busio.UART to talk to real hardware
ctpy.runPython(`
import busio
import board

uart = busio.UART(board.TX, board.RX, baudrate=115200)
uart.write(b'Hello from CircuitPython WASM!\\n')

response = uart.read(32)
print(f"Received: {response}")
`);

// Cleanup
process.on('SIGINT', () => {
    serial.close();
    process.exit(0);
});
```

---

## Summary

- **Layer 1 (Minimal)**: Direct API access, attach only needed peripherals
- **Layer 2 (Batteries-Included)**: Use `CircuitPythonBoard` for complete experience
- **Custom Peripherals**: Implement device simulations for testing/development
- **Node.js**: Perfect for automated testing and CLI tools

Choose the approach that best fits your use case!
