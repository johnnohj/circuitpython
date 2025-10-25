# CircuitPython WASM Port Design Philosophy

## The Triple Nature of WASM

WASM is simultaneously three things that are separate in hardware:

| Layer | Hardware | WASM |
|-------|----------|------|
| **Port** | ARM Cortex-M, RISC-V, etc. | Emscripten/WebAssembly compilation |
| **Silicon** | The actual chip (RP2040, SAMD51, etc.) | Virtual hardware simulation (`virtual_hardware.c`) |
| **Board** | PCB with specific pins/peripherals wired | Configuration of which peripherals exist |

In hardware, these are physically separate:
- **Port**: The CPU architecture (different compiler, different instructions)
- **Silicon**: The actual chip design (GPIO registers at specific addresses)
- **Board**: How the chip is wired on a PCB (which pins go where)

**In WASM, these collapse into one:**
- We compile to WASM (port)
- We simulate the chip (silicon)
- We configure what exists (board)

All in the same codebase.

## Proposed Layer Architecture

### Layer 1: Port (`ports/webassembly/`)
**Responsibility**: WASM/Emscripten-specific infrastructure that would be the same regardless of what we're simulating.

**Files:**
- `Makefile` - Build orchestration (Emscripten flags, JS library integration)
- `mpconfigport.h` - Python VM configuration for WASM
- `mphalport.c/h` - HAL for WASM/Emscripten (timing, stdout, etc.)
- `main.c` - Entry point, REPL setup
- **C/JS Boundary:**
  - `library.js` - Emscripten library functions (exported to WASM)
  - `api.js` - JavaScript API (what developers use)

### Layer 2: Silicon (`ports/webassembly/silicon/`)
**Responsibility**: The virtual hardware simulation - the "chip" we're simulating.

**Files:**
- `virtual_hardware.c/h` - Hardware simulation (GPIO, I2C, SPI, UART state)
- `shared_memory.h` - C/JS shared memory layout (the "hardware registers")
- `virtual_clock.c/h` - Virtual timing system
- **Common-HAL implementations** (`common-hal/*/`)
  - These call into virtual_hardware functions
  - This is where Python `pin.value = True` becomes `virtual_gpio_set_value()`

**JavaScript Runtime Integration:**
- `virtual_clock.js` - JS side of timing
- `virtual_gpio.js` - JS side of GPIO (future)
- `virtual_i2c.js` - JS side of I2C simulation (future)

### Layer 3: Variant (`ports/webassembly/variants/VARIANT/`)
**Responsibility**: Different simulation configurations, NOT different physical boards.

**Variants we might have:**
- `standard/` - Full CircuitPython with all common modules
- `minimal/` - Bare minimum for testing (like MicroPython unix/minimal)
- `display/` - Standard + displayio for graphics testing
- `networking/` - Standard + wifi/socketpool for network testing
- `sensors/` - Standard + common sensor libraries pre-loaded

**Files in each variant:**
- `mpconfigvariant.mk` - Which CircuitPython modules to enable
- `mpconfigvariant.h` - Variant-specific Python VM settings (if needed)

**NO board-specific pins.c** - Pins are virtual, defined once at silicon level.

## The C/JS Boundary

This is critical and currently unclear. Here's the proposed clean separation:

### C Side Exports (WASM → JavaScript)
**In `library.js` (Emscripten `--js-library`):**
```c
// These are the ONLY functions C calls into JavaScript:
EM_JS(void, js_gpio_changed, (int pin, int value), {
    // Notify JS that GPIO changed
});

EM_JS(void, js_i2c_transaction, (int address, uint8_t* data, int len), {
    // Notify JS of I2C transaction
});
```

### JavaScript Side Exports (JavaScript → WASM)
**In `api.js` (loaded via `--pre-js`):**
```javascript
// These are the ONLY functions JavaScript calls into C:
export async function loadCircuitPython(options) {
    const Module = await _createCircuitPythonModule();

    return {
        // High-level API
        runPython: (code) => Module.ccall('mp_js_do_str', ...),
        runFile: (path) => Module.ccall('mp_js_do_file', ...),

        // Hardware access (read-only from JS)
        getGPIOValue: (pin) => Module._virtual_gpio_get_value(pin),
        getI2CState: () => Module._virtual_i2c_get_state(),

        // Virtual clock control (JS drives the clock)
        advanceTime: (ms) => Module._virtual_clock_advance(ms),

        // Filesystem
        FS: Module.FS,
    };
}
```

## Ownership and Responsibility

| Component | Owner | Responsibility |
|-----------|-------|----------------|
| **CircuitPython VM** | CircuitPython core | Python execution |
| **Supervisor** | CircuitPython core | Boot sequence, timing, safe mode |
| **Common-HAL** | Port (webassembly) | Hardware abstraction API implementation |
| **Virtual Hardware** | Port (silicon layer) | Simulated chip state |
| **Shared Memory** | Port (silicon layer) | C/JS data bridge |
| **JavaScript Runtime** | Port (api.js) | Browser/Node.js integration |
| **Virtual Devices** | TypeScript/JS | Simulated I2C devices, displays, etc. |

## Build Flow

```
User runs: make VARIANT=standard

1. circuitpy_mkenv.mk loads variant configuration
2. mpconfigvariant.mk sets CIRCUITPY_* flags
3. circuitpy_defns.mk discovers modules to build based on flags
4. Port Makefile compiles:
   - CircuitPython core (py/)
   - Supervisor (supervisor/)
   - Common-HAL implementations (common-hal/)
   - Virtual hardware (silicon/)
   - Port infrastructure (mphalport.c, etc.)
5. Emscripten links everything to circuitpython.wasm
6. api.js wraps WASM in JavaScript API
7. Output: build-VARIANT/circuitpython.mjs + circuitpython.wasm
```

## Why Variant, Not Board?

**Board abstraction implies:**
- Different physical hardware layouts
- Different pin mappings (board.D0 on Pico vs Feather)
- Different peripheral availability (this board has WiFi, that doesn't)

**Variant abstraction implies:**
- Different feature sets of the same virtual platform
- Different module combinations for different use cases
- Same "hardware" (all 64 GPIO, all peripherals), just different software builds

For WASM, we're NOT representing different physical boards. We're representing different **software configurations** of the same virtual platform. That's a variant, not a board.

## Next Steps

1. Restructure to variant-based build
2. Move virtual_hardware to `silicon/` subdirectory for clarity
3. Define clean C/JS boundary with documented exports
4. Create multiple variants (minimal, standard, display)
5. Document the "silicon specification" - what hardware we're simulating

This gives us:
- **Clear ownership** - Who owns each layer
- **Clean boundaries** - Well-defined interfaces between layers
- **Honest naming** - We're not pretending to be physical boards
- **Flexibility** - Easy to add new simulation features or variants
