# CircuitPython WASM Port Architecture

## CircuitPython Configuration Layers (From Bottom Up)

### Layer 1: Core Python (`py/`)
- `py/circuitpy_mkenv.mk` - CircuitPython environment setup
- `py/circuitpy_defns.mk` - Automatic module/source discovery based on CIRCUITPY_* flags  
- `py/py.mk` - Core Python source files
- `py/mkrules.mk` - Build rules

### Layer 2: Port Configuration (`ports/webassembly/`)
- `Makefile` - Port-specific build orchestration
- `mpconfigport.h` - Port-wide Python configuration (what Python features are enabled)
- Port source files (`mphalport.c`, `main.c`, etc.)

### Layer 3: Board Configuration (`ports/webassembly/boards/BOARD/`)
- `mpconfigboard.h` - Board hardware configuration (pins, peripherals)
- `mpconfigboard.mk` - Which CircuitPython modules to enable (CIRCUITPY_ANALOGIO=1, etc.)
- `pins.c` - Board pin definitions
- `board.c` - Board-specific initialization

### Layer 4: WASM-Specific Integration
**This is where we need clarity!**

Current files without clear layer assignment:
- `shared_memory.c/h` - Virtual hardware state (belongs at port level)
- `virtual_hardware.c/h` - Hardware simulation (belongs at port level)  
- `background.c/h` - Background task coordination (belongs at port level)
- JavaScript files (api.js, virtual_clock.js, etc.) - WASM loader/runtime

## Full CircuitPython Port Requirements

### A. Supervisor Layer (`supervisor/`)
**We have:**
- ✅ `supervisor/port.c` - Port initialization, timing, idle
- ✅ `supervisor/shared/tick.c` - Timing system
- ✅ `supervisor/shared/background_callback.c` - Background tasks
- ✅ `supervisor/shared/translate/translate.c` - String compression/decompression

**We're missing:**
- ❌ Heap management (`port_heap_init`, `port_malloc`, `port_free`)
- ❌ Boot sequence integration (boot.py, code.py auto-run)
- ❌ USB/Serial integration
- ❌ Safe mode handling
- ❌ CircuitPython REPL prompt

### B. Hardware Abstraction (`common-hal/`)
**We have:**
- ✅ analogio (AnalogIn, AnalogOut)  
- ✅ busio (I2C, SPI, UART)
- ✅ digitalio (DigitalInOut)
- ✅ microcontroller (Pin, Processor, cpu)
- ✅ time (monotonic, sleep)

**Integration status:**
- ⚠️ Hardware is defined but NOT connected to virtual_hardware simulation
- ⚠️ No GPIO state actually changes in JavaScript when Python calls GPIO functions

### C. Module Ecosystem
**Currently enabled (7 modules):**
- analogio, board, busio, digitalio, microcontroller, time, zlib

**Typical full build (40+ modules):**
- USB (usb_cdc, usb_hid, usb_midi)
- Storage (storage, os, sys)  
- Display (displayio, vectorio, terminalio)
- Networking (wifi, socketpool, ssl)
- Audio (audiocore, audioio)
- etc.

## Key Integration Gaps

### 1. Virtual Hardware Not Wired Up
```c
// common-hal/digitalio/DigitalInOut.c does:
void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self, bool value) {
    // This writes to some local state...
    // But virtual_gpio_set_output_value() is NEVER called!
    // So JavaScript never sees the GPIO change!
}
```

### 2. Timing System Incomplete  
- `port_get_raw_ticks()` reads from `virtual_clock_hw.ticks_32khz`
- But supervisor timing may not be properly using this
- Need to verify timing integration end-to-end

### 3. Memory Management Missing
- Using Emscripten's malloc instead of CircuitPython's managed heap
- No `gc_collect()` integration with port heap
- No heap statistics

### 4. Boot Sequence Missing
- No supervisor boot (boot.py, code.py)
- No USB/serial console
- No CircuitPython REPL prompt (just bare MicroPython REPL)

## Questions to Answer

1. **Board vs Variant**: Do we even need board abstraction for WASM? Or should WASM be a "port" with different "configurations" (similar to Unix port variants)?

2. **Virtual Hardware Ownership**: Should `virtual_hardware.c` be:
   - At port level (webassembly/virtual_hardware.c) ✓ current
   - Part of common-hal (webassembly/common-hal/virtual/hardware.c)
   - Shared infrastructure (supervisor/virtual_hardware.c)

3. **JavaScript Integration**: Where should the JS boundary be?
   - Current: Scattered (mphalport exports, library.js exports, etc.)
   - Better: Defined interface layer?

4. **Configuration Philosophy**: 
   - MicroPython approach: Variants for different configs (standard, minimal, etc.)
   - CircuitPython approach: Boards for different hardware
   - WASM needs: Different simulation configurations?

