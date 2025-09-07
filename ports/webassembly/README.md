# CircuitPython WebAssembly Port

The CircuitPython WebAssembly port is a JavaScript-backed WebAssembly implementation that enables CircuitPython to run in web browsers and Node.js environments. This port implements hardware interfaces through JavaScript simulation, making it ideal for education, prototyping, and web-based CircuitPython development.

For general CircuitPython building information, refer to the [Adafruit Learn guide: Building CircuitPython](https://learn.adafruit.com/building-circuitpython).

## Building

### Prerequisites

Building the WebAssembly port requires the Emscripten WebAssembly toolchain in addition to standard CircuitPython build dependencies:

1. **Emscripten SDK (emsdk)**:
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source ./emsdk_env.sh
   ```

2. **Standard CircuitPython dependencies**:
   - Python 3.x
   - Git
   - GNU Make
   - Standard build tools (gcc/clang)

### Build Steps

From the CircuitPython root directory:

```bash
cd ports/webassembly
make -C ../../mpy-cross
make
```

This produces `build-standard/circuitpython.mjs` - a WebAssembly module with JavaScript bindings.

## JavaScript API

The WebAssembly port exposes a comprehensive JavaScript API for hardware simulation and CircuitPython interaction.

### Core API Structure

The compiled output provides:

- **CircuitPython REPL**: Interactive Python environment
- **Hardware Simulation**: JavaScript-backed GPIO, ADC/DAC, I2C, SPI interfaces  
- **Board Definition**: Configurable pin mapping and hardware layout
- **Proxy System**: Bidirectional communication between Python and JavaScript

### Pin and Board API

```javascript
// Pin object structure
const pin = {
  number: 25,           // Pin number
  name: "GP25",         // Pin name
  digitalIO: null,      // DigitalInOut reference
  analogIO: null,       // AnalogIn/AnalogOut reference
  
  // Create digital I/O interface
  createDigitalIO: function() { /* ... */ },
  
  // Create analog input interface  
  createAnalogIn: function() { /* ... */ },
  
  // Create analog output interface
  createAnalogOut: function() { /* ... */ }
};

// Board object with pin definitions
const board = {
  GP0: { number: 0, name: "GP0", /* ... */ },
  GP1: { number: 1, name: "GP1", /* ... */ },
  // ... additional pins
};
```

### Digital I/O Simulation

```javascript
// Digital I/O object methods
const digitalIO = {
  direction: "input",    // "input" or "output"
  value: false,          // Current pin state
  pull: "none",          // "up", "down", or "none"
  
  // Set pin direction
  setDirection: function(direction) { /* ... */ },
  
  // Set/get pin value
  setValue: function(value) { /* ... */ },
  getValue: function() { /* ... */ },
  
  // Configure pull resistor
  setPull: function(pull) { /* ... */ },
  
  // Cleanup
  deinit: function() { /* ... */ }
};
```

### Analog I/O Simulation

```javascript
// Analog Input (ADC simulation)
const analogIn = {
  referenceVoltage: 3.3,   // Reference voltage
  
  // Get simulated ADC value (0-65535)
  getValue: function() {
    return Math.floor(Math.random() * 65536);
  },
  
  deinit: function() { /* ... */ }
};

// Analog Output (DAC simulation)  
const analogOut = {
  value: 0,                // Current output value (0-65535)
  
  // Set DAC output value
  setValue: function(value) { /* ... */ },
  
  deinit: function() { /* ... */ }
};
```

### I2C Bus Simulation

```javascript
// Global I2C creation function
function createI2C(sclPin, sdaPin, frequency) {
  return {
    locked: false,
    devices: new Map(),    // Simulated I2C device memory
    
    // Bus locking
    tryLock: function() { /* ... */ },
    hasLock: function() { /* ... */ },
    unlock: function() { /* ... */ },
    
    // Device communication
    scan: function(address) { /* returns 0 if device present */ },
    writeto: function(address, data, stop) { /* ... */ },
    readfrom: function(address, length) { /* ... */ },
    writeto_then_readfrom: function(address, writeData, readLength) { /* ... */ },
    
    deinit: function() { /* ... */ }
  };
}
```

### SPI Bus Simulation

```javascript
// Global SPI creation function  
function createSPI(clockPin, mosiPin, misoPin) {
  return {
    locked: false,
    baudrate: 100000,
    polarity: 0,
    phase: 0,
    bits: 8,
    
    // Configuration
    configure: function(baudrate, polarity, phase, bits) { /* ... */ },
    
    // Bus locking
    tryLock: function() { /* ... */ },  
    hasLock: function() { /* ... */ },
    unlock: function() { /* ... */ },
    
    // Data transfer
    write: function(data) { /* ... */ },
    readinto: function(buffer, writeValue) { /* ... */ },
    transfer: function(writeData, readData) { /* ... */ },
    
    deinit: function() { /* ... */ }
  };
}
```

## Usage Examples

### Node.js Integration

```javascript
// app.js
const circuitpython = require('./build-standard/circuitpython.mjs');

// Initialize CircuitPython runtime
circuitpython.then(cp => {
  // Execute CircuitPython code
  cp.runPython(`
import board
import digitalio

# Create LED on pin GP25
led = digitalio.DigitalInOut(board.GP25)
led.direction = digitalio.Direction.OUTPUT
led.value = True

print("LED turned on!")
  `);
});
```

### Web Browser Integration

```html
<!DOCTYPE html>
<html>
<head>
    <title>CircuitPython in Browser</title>
</head>
<body>
    <div id="output"></div>
    <script type="module">
        import circuitpython from './build-standard/circuitpython.mjs';
        
        circuitpython().then(cp => {
            // Redirect output to webpage
            cp.setStdout((text) => {
                document.getElementById('output').innerHTML += text + '<br>';
            });
            
            // Run CircuitPython REPL
            cp.runREPL();
            
            // Or execute specific code
            cp.runPython(`
import board
import analogio
import busio

# Test analog input
adc = analogio.AnalogIn(board.GP26)
print(f"ADC value: {adc.value}")

# Test I2C bus scan
i2c = busio.I2C(board.GP0, board.GP1)
i2c.try_lock()
devices = i2c.scan()
print(f"I2C devices found: {devices}")
i2c.unlock()
            `);
        });
    </script>
</body>
</html>
```

### Interactive REPL Usage

The WebAssembly port provides a full CircuitPython REPL experience. Use the provided REPL script:

```bash
$ node circuitpython_repl.js
Loading CircuitPython WebAssembly port...

Adafruit CircuitPython 10.0.0-beta.2-23-g70344535d3-dirty on 2025-09-05; WebAssembly with Emscripten
Type "help()" for more information, Ctrl+C to exit

>>> import board
>>> import digitalio
>>> led = digitalio.DigitalInOut(board.GP25)
>>> led.direction = digitalio.Direction.OUTPUT
>>> led.value = True
>>> print("GPIO simulation active!")
GPIO simulation active!
>>> 
```

**Note**: The raw `node build-standard/circuitpython.mjs` command loads the WebAssembly module but doesn't automatically start the REPL. Use `circuitpython_repl.js` for interactive sessions.

#### Direct WebAssembly Module Usage

For programmatic access, you can use the WebAssembly module directly:

```javascript
import circuitpython from './build-standard/circuitpython.mjs';

const Module = await circuitpython();

// Initialize CircuitPython with 1MB heap
Module._mp_js_init_with_heap(1024 * 1024);
Module._proxy_c_init();

// Execute Python code directly
const output = new Uint32Array(3);
const pythonCode = "import board; print('Hello from CircuitPython!')";
Module._mp_js_do_exec(pythonCode, pythonCode.length, output);
```

### Hardware Simulation Features

The JavaScript backend provides realistic hardware simulation:

#### Digital I/O
- Pin state tracking and change detection
- Pull-up/pull-down resistor simulation
- Direction control (input/output)

#### Analog Interfaces  
- 16-bit ADC simulation with noise
- Configurable reference voltage
- DAC output value tracking

#### I2C Bus
- Multi-device memory simulation
- Bus scanning and device detection
- Read/write operations with realistic timing
- Proper bus locking mechanisms

#### SPI Interface
- Configurable clock, polarity, phase settings
- Full-duplex data transfer simulation
- Multiple device support
- Realistic timing behavior

## Input/Output Specifications

### Input Format
- **Python Code**: Standard CircuitPython syntax and modules
- **REPL Commands**: Interactive Python statements and expressions
- **Configuration**: JavaScript object literals for hardware setup

### Output Format
- **STDOUT**: Python print statements and REPL output
- **Hardware State**: JavaScript object properties and callbacks
- **Error Messages**: Standard Python tracebacks and exceptions
- **Debug Info**: Optional logging for hardware simulation events

### Data Types and Ranges
- **Digital Values**: Boolean (True/False) or integers (0/1)
- **Analog Values**: 16-bit integers (0-65535) representing voltage levels
- **I2C/SPI Data**: Byte arrays and Uint8Array objects
- **Pin Numbers**: Integers corresponding to board pin definitions

## Advanced Features

### Custom Hardware Simulation

You can extend the hardware simulation by modifying the JavaScript API:

```javascript
// Custom sensor simulation
board.GP27.createCustomSensor = function() {
  return {
    temperature: 25.0,
    humidity: 50.0,
    
    readTemperature: function() {
      // Simulate temperature drift
      this.temperature += (Math.random() - 0.5) * 0.1;
      return this.temperature;
    },
    
    readHumidity: function() {
      // Simulate humidity changes
      this.humidity += (Math.random() - 0.5) * 2.0;
      return Math.max(0, Math.min(100, this.humidity));
    }
  };
};
```

### Debugging and Monitoring

The WebAssembly port provides debugging hooks for monitoring hardware operations:

```javascript
// Enable hardware operation logging
const originalCreateDigitalIO = board.GP25.createDigitalIO;
board.GP25.createDigitalIO = function() {
  const digitalIO = originalCreateDigitalIO.call(this);
  const originalSetValue = digitalIO.setValue;
  
  digitalIO.setValue = function(value) {
    console.log(`Pin ${this.number} set to ${value}`);
    return originalSetValue.call(this, value);
  };
  
  return digitalIO;
};
```

This JavaScript-backed CircuitPython port enables powerful web-based development and simulation capabilities while maintaining full compatibility with standard CircuitPython APIs and workflows.