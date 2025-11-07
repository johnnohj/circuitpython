# CircuitPython WebAssembly Port

This is an implementation of CircuitPython compiled to WebAssembly for browser-based execution. This port enables CircuitPython to run entirely in a web browser with simulated hardware peripherals.

## ⚠️ Status: Experimental

This port is provided on a **best-effort basis** and is under active development. It is designed primarily for:
- Educational purposes and learning CircuitPython
- Web-based development environments
- Hardware prototyping and visualization
- Demonstrating CircuitPython concepts without physical hardware

**This is NOT intended for production embedded systems.**

## Architecture Overview

### Worker-First Design
The WASM port is architected to run CircuitPython in a Web Worker, keeping the browser UI responsive while Python code executes. This fundamental design decision enables:
- Non-blocking execution of Python code
- Real-time hardware state updates via `postMessage`
- Responsive UI during long-running operations
- Clean separation between Python execution and visualization

### Build System Challenges
One of the primary challenges in maintaining this port is navigating between **MicroPython's original build structure** and **CircuitPython's enhanced port architecture**:

**MicroPython WASM Port:**
- Original WebAssembly port implementation
- Simpler build system with minimal configuration
- Focused on pure Python execution
- Limited hardware abstraction

**CircuitPython WASM Port (this port):**
- Built on MicroPython's WASM foundation
- Extensive hardware peripheral modules (digitalio, analogio, busio, etc.)
- Supervisor system for background tasks and timing
- Hardware-centric APIs and conventions
- More complex build with shared bindings and common-hal layers

**The Challenge:** CircuitPython's supervisor assumes physical hardware with interrupts, tick timers, and real peripherals. Adapting these assumptions for a browser environment while maintaining CircuitPython's API compatibility requires careful architectural decisions throughout the port.

Key areas of complexity:
- Supervisor timing mechanisms vs. browser event loop
- Hardware abstraction layers (common-hal) for virtual devices
- Shared bindings that expect real hardware registers
- Memory management across C/WASM/JavaScript boundaries
- Balancing CircuitPython API compatibility with browser constraints

## Virtual Hardware Implementation

### Design Philosophy
Unlike physical hardware ports that interact with real GPIO pins and peripherals, the WASM port implements "virtual hardware" as in-memory state arrays that are:
- Accessible from both C (via Emscripten) and JavaScript (via shared memory or message passing)
- Visualized in real-time in the web browser
- Updated via direct `postMessage()` for streaming updates

### Key Challenge: The Supervisor Problem
CircuitPython's supervisor is designed for physical microcontrollers with hardware interrupts, tick timers, and blocking operations. Adapting this for WASM required:

**Problem:** The default `mp_hal_delay_ms()` waits for `raw_ticks` to increment via JavaScript's `setInterval()`, but `setInterval` can't run when the worker thread is blocked.

**Solution:** Override `mp_hal_delay_ms()` to use `emscripten_get_now()` (Date.now()) for a pure busy-wait that doesn't rely on external tick sources.

```c
// WASM-specific non-blocking delay
void mp_hal_delay_ms(mp_uint_t delay_ms) {
    double start_time = emscripten_get_now();
    double target_time = start_time + delay_ms;
    while (emscripten_get_now() < target_time) {
        // Busy-wait allows continued execution
    }
}
```

This allows GPIO updates to stream in real-time even during `time.sleep()` calls.

## Implemented and Tested Features

### ✅ Core Modules
- **os** - File system operations (getcwd, chdir, listdir, mkdir, remove, rename, stat)
- **time** - Non-blocking sleep, monotonic time, time functions
- **board** - Pin definitions (64 GPIO pins: D0-D63)

### ✅ Hardware Peripherals
- **digitalio.DigitalInOut** - GPIO with direction, pull resistors, drive modes
  - Real-time state updates via push architecture
  - Hardware panel visualization with live blinking
  - Support for input/output modes, pull-up/pull-down resistors

- **analogio.AnalogIn** - Analog input simulation (implementation complete, visualization pending)

- **busio** - Communication protocols with JavaScript integration
  - I2C controller with device scanning
  - UART with configurable baud rates
  - SPI with CPOL/CPHA configuration

### ✅ Infrastructure
- **VFS (Virtual File System)** - Unix-style directory structure (/home, /tmp)
- **Dual Filesystem** - IndexedDB (persistence) + VFS (execution) with path synchronization
- **JsProxy System** - Bidirectional C ↔ JavaScript object proxying
- **Peripheral Hooks** - `Module.getPeripheral(name)` for hardware abstraction
- **Worker Proxy** - Message-based API for main thread ↔ worker communication

### ✅ Web Editor Integration
- **Save+Run** - Write code to IndexedDB and VFS, execute immediately
- **Hardware Panel** - Live GPIO visualization with automatic terminal resizing
- **Terminal** - xterm.js-based serial output with proper layout handling
- **File Management** - Open, save, create files in IndexedDB

## Known Working Examples

```python
# GPIO Blinking with Real-Time Visualization
import digitalio
import board
import time

led = digitalio.DigitalInOut(board.D0)
led.direction = digitalio.Direction.OUTPUT

for i in range(10):
    led.value = True
    time.sleep(0.5)
    led.value = False
    time.sleep(0.5)
# Hardware panel shows pin blinking in real-time!
```

```python
# File System Operations
import os

os.mkdir('/home/test')
os.chdir('/home/test')
print(os.getcwd())  # /home/test

with open('data.txt', 'w') as f:
    f.write('Hello from WASM!')
```

```python
# I2C Device Scanning
import busio
import board

i2c = busio.I2C(board.SCL, board.SDA)
while not i2c.try_lock():
    pass

devices = i2c.scan()
print(f"Found {len(devices)} I2C devices")
i2c.unlock()
```

## Still Needs Work

### 🚧 Hardware Visualization
- [ ] Analog input visualization (implementation exists, UI pending)
- [ ] PWM visualization (duty cycle meters, frequency indicators)
- [ ] NeoPixel visualization (LED strip display)
- [ ] I2C/UART/SPI protocol analyzers
- [ ] Interactive hardware controls (buttons, sliders for input simulation)
- [ ] Extend postMessage pattern to other peripherals for real-time updates during `time.sleep()`
  - PWM duty cycle changes (high priority - motors, servos, LED brightness)
  - NeoPixel color changes (high priority - visual feedback)
  - UART TX/RX streaming (medium priority - serial debugging)
  - Analog input readings (medium priority - sensor monitoring)
  - I2C/SPI transactions (medium priority - protocol debugging)

### 🚧 Modules & Features
- [ ] audioio/audiobusio - Audio playback and capture
- [ ] displayio - Display driver framework
- [ ] touchio - Capacitive touch inputs
- [ ] rotaryio - Rotary encoder support
- [ ] pulseio - PulseIn/PulseOut for IR, DHT sensors
- [ ] asyncio - Asynchronous programming support
- [ ] Network/WiFi simulation

### 🚧 Developer Experience
- [ ] Comprehensive test suite
- [ ] Performance profiling and optimization
- [ ] Better error messages and debugging
- [ ] Developer documentation for adding peripherals
- [ ] Example projects and tutorials
- [ ] CI/CD integration

### 🚧 Known Issues
- Random number generation uses placeholder (needs crypto.getRandomValues())
- No persistent settings across browser sessions
- Limited to ~512KB heap size (browser constraint)
- Worker thread busy-wait impacts battery on mobile devices

## Development Workflow

### Building the WASM Port

```bash
cd circuitpython/ports/wasm
make
```

This generates:
- `build-standard/circuitpython.mjs` - Main module
- `build-standard/circuitpython.wasm` - WebAssembly binary

Copy these to `/web-editor/public/wasm/`:
```bash
cp build-standard/circuitpython.* ../../web-editor/public/wasm/
```

### Testing Changes

1. Build the WASM port (see above)
2. Start the web editor dev server:
   ```bash
   cd web-editor
   npm install
   npm run dev
   ```
3. Open browser to `http://localhost:8080`
4. Select "Virtual" workflow
5. Test your changes

### Adding New Peripherals

1. **C Implementation** - Create `common-hal/<module>/<Class>.c`
2. **State Array** - Define virtual hardware state (e.g., `gpio_state[]`)
3. **EM_JS Functions** - Bridge to JavaScript for visualization
4. **JavaScript Controller** - Create peripheral in `web-editor/public/wasm/library.js`
5. **Peripheral Registration** - Register via `Module.addPeripheral(name, controller)`
6. **Hardware Panel** - Add visualization to `web-editor/js/common/hardware-panel.js`

Example pattern:
```c
// C: Post state updates
EM_JS(void, peripheral_post_update, (int param), {
    self.postMessage({type: 'peripheral-update', param: param});
});

// JS: Handle updates
worker.onmessage = function(e) {
    if (e.data.type === 'peripheral-update') {
        updateVisualization(e.data.param);
    }
};
```

## Contributing

This is an experimental port with many rough edges. Contributions are welcome, particularly:
- Bug fixes for existing features
- Additional peripheral implementations
- Hardware visualization improvements
- Documentation and examples
- Performance optimizations

Please note that this port follows CircuitPython's contributor guidelines and code of conduct.

## Technical Notes

### Memory Management
- Heap size: 512KB (configurable in `web-editor/js/workflows/virtual.js`)
- Python stack: 8K words
- VFS is in-memory only (no persistent storage between page reloads)
- IndexedDB provides persistence for user files

### Timing Accuracy
- `time.sleep()` uses busy-wait for accurate timing
- `time.monotonic()` based on `emscripten_get_now()` (JavaScript `Date.now()`)
- No real-time guarantees (browser scheduler dependent)

### JavaScript Integration
- JsProxy allows Python code to call JavaScript functions
- Peripheral hooks enable clean hardware abstraction
- Message passing keeps worker and main thread synchronized

## License

Same as CircuitPython: MIT License

## Acknowledgments

This port builds on the foundation of the original MicroPython WebAssembly implementation. Special thanks to:

- **The MicroPython team** for creating the original WASM port and the excellent MicroPython architecture that makes ports like this possible
- **The CircuitPython team** for their extensive documentation, hardware-centric design philosophy, and clean port architecture that enables educational hardware projects
- **The Emscripten team** for their amazing compiler toolchain that makes WebAssembly development practical

## Links

- [MicroPython](https://github.com/micropython/micropython) - The original Python for microcontrollers
- [CircuitPython](https://github.com/adafruit/circuitpython) - Hardware-focused Python for education and prototyping
- [Emscripten](https://github.com/emscripten-core/emscripten) - C/C++ to WebAssembly compiler
- [CircuitPython Code Editor](https://code.circuitpython.org)
- [Web Editor with WASM Support](https://github.com/johnnohj/web-editor/tree/wasm) - Fork of the web-editor with CircuitPython WASM integration