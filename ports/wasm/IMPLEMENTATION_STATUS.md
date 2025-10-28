# CircuitPython WASM Port - Implementation Status

This document tracks which CircuitPython modules are implemented in the WASM port.

## Fully Implemented ✅

### digitalio
- **DigitalInOut** - GPIO pins (input/output, pull resistors, drive modes)
  - State stored in `gpio_state[]` array in [common-hal/digitalio/DigitalInOut.c](common-hal/digitalio/DigitalInOut.c)
  - JavaScript can access via `mp_js_gpio_*` functions in library.js

### analogio  
- **AnalogIn** - ADC (read analog values)
- **AnalogOut** - DAC (output analog values)
  - State stored in `analog_state[]` array in [common-hal/analogio/AnalogIn.c](common-hal/analogio/AnalogIn.c)
  - JavaScript can access via `mp_js_analog_*` functions in library.js

### microcontroller
- **Processor** - CPU info (frequency, temperature, voltage)
- **Pin** - Pin definitions and management
- **__init__** - Reset, delays, interrupt control

### board
- **__init__** - Board-specific pin definitions (D0-D63)

### time
- **__init__** - time.sleep(), time.monotonic(), etc.
  - Uses virtual_clock_hw in supervisor/port.c

### pwmio
- **PWMOut** - PWM output for servos, LEDs, motor control
  - State stored in `pwm_state[]` array in [common-hal/pwmio/PWMOut.c](common-hal/pwmio/PWMOut.c)
  - JavaScript can access via `mp_js_pwm_*` functions in [library_pwmio.js](library_pwmio.js)
  - Supports variable and fixed frequency modes
  - Duty cycle: 16-bit (0-65535)
  - Default frequency: 500Hz

### neopixel_write
- **neopixel_write()** - Write data to NeoPixel/WS2812 LED strips
  - State stored in `neopixel_state[]` array in [common-hal/neopixel_write/__init__.c](common-hal/neopixel_write/__init__.c)
  - JavaScript can access via `Module.getNeoPixelData()` in [library_neopixel.js](library_neopixel.js)
  - Supports up to 256 LEDs per pin
  - Per-pin pixel buffers with RGB data
  - Ideal for LED strip visualization in browser


### busio
- **I2C** - I2C bus communication
  - State stored in `i2c_buses[]` array in [common-hal/busio/I2C.c](common-hal/busio/I2C.c)
  - JavaScript can access via `Module.setI2CDevice()` and `Module.getI2CDeviceRegisters()` in [library_busio.js](library_busio.js)
  - Supports up to 8 I2C buses with different pin pairs
  - Each bus supports 128 device addresses (7-bit addressing)
  - Each device has 256 bytes of register space
  - Tracks last read/write transactions for debugging
- **SPI** - SPI bus communication (not yet implemented)
- **UART** - Serial UART communication
  - State stored in `uart_ports[]` array in [common-hal/busio/UART.c](common-hal/busio/UART.c)
  - JavaScript can access via `Module.writeUARTRx()` and `Module.readUARTTx()` in [library_busio.js](library_busio.js)
  - Supports up to 8 UART ports with independent TX/RX pins
  - Ring buffers for TX and RX (512 bytes each)
  - Configurable baudrate, parity, stop bits
  - Supports TX-only, RX-only, or full-duplex modes

## Not Yet Implemented ❌

### High Priority (Common Use Cases)

#### displayio
- **Display** - Graphics display abstraction
- **File:** Would be `common-hal/displayio/`
- **State:** Framebuffer array
- **JavaScript:** Render to Canvas element
- **Priority:** HIGH - enables graphics/visualization!

### Medium Priority (Useful Features)

#### rotaryio
- **IncrementalEncoder** - Rotary encoder support
- **File:** Would be `common-hal/rotaryio/IncrementalEncoder.c`

#### audiobusio / audioio
- **I2SOut** - Audio output
- **File:** Would be `common-hal/audiobusio/`
- **JavaScript:** Play via Web Audio API

#### watchdog
- **WatchDogTimer** - Watchdog timer
- **File:** Would be `common-hal/watchdog/WatchDogTimer.c`

#### storage
- **Storage** - Filesystem access
- Already have filesystem_wasm.c for basic support

### Lower Priority (Advanced/Specific)

#### wifi / socketpool
- **File:** Would be `common-hal/wifi/`, `common-hal/socketpool/`
- **Note:** Could use browser's fetch API!

#### alarm
- **File:** Would be `common-hal/alarm/`
- **Note:** Deep sleep doesn't make sense for WASM

#### bleio
- **File:** Would be `common-hal/bleio/`
- **Note:** Could use Web Bluetooth API!

#### camera
- **File:** Would be `common-hal/camera/`
- **JavaScript:** Access getUserMedia()

## How to Add a New Module

1. **Check shared-bindings** for required common_hal_* functions:
   ```bash
   grep "common_hal_" circuitpython/shared-bindings/<module>/*.h
   ```

2. **Create stub file** in `ports/wasm/common-hal/<module>/`:
   ```c
   // TODO: Implement <module> for WASM port
   // State array pattern:
   <module>_state_t <module>_state[64];
   
   EMSCRIPTEN_KEEPALIVE
   <module>_state_t* get_<module>_state_ptr(void) {
       return <module>_state;
   }
   ```

3. **Add to Makefile**:
   ```makefile
   SRC_COMMON_HAL += common-hal/<module>/<Class>.c
   ```

4. **Implement reset** in supervisor/port.c:
   ```c
   void reset_port(void) {
       ...
       <module>_reset_state();  // Add this
   }
   ```

5. **Add JavaScript access** in library-*.js:
   ```javascript
   mp_js_<module>_get: (pin) => { /* access state array */ }
   ```

6. **Export pointer** in Makefile:
   ```makefile
   EXPORTED_FUNCTIONS_EXTRA += _get_<module>_state_ptr,\
   ```

## Next Steps

The most impactful modules to implement next would be:

1. **displayio** - Graphics/visualization (aligns with user's vision!) - Complex module with multiple classes
2. **busio** - Complete I2C/SPI/UART implementations - Would enable sensor simulation
3. **rotaryio** - Rotary encoder support for UI controls

All follow the same pattern: state arrays in common-hal, JavaScript access via library_*.js modules.

## Recent Progress (This Session)

✅ **PWMOut** - Fully implemented with variable/fixed frequency modes, duty cycle control
✅ **neopixel_write** - Fully implemented with support for up to 256 LEDs per pin
✅ **busio.I2C** - Fully implemented with virtual device simulation, register storage, and transaction tracking
✅ **busio.UART** - Fully implemented with ring buffers, TX/RX support, and configurable serial parameters

## Session Progress Summary

### Completed ✅
- **PWMOut** - Full PWM support with variable/fixed frequency modes
- **neopixel_write** - LED strip support with up to 256 LEDs per pin  
- **busio.I2C** - Full I2C implementation with virtual device simulation

### In Progress 🔄
- **busio.UART** - Implementation complete (ring buffers, TX/RX, baudrate control) but blocked by CircuitPython ioctl signature compatibility issue between architecture types

### Architecture Pattern Established
All modules follow consistent pattern:
1. State arrays in `common-hal/` (virtual hardware)
2. JavaScript bridge via `library_*.js` modules
3. Reset functions in `supervisor/port.c`  
4. Proper Makefile integration
5. Configuration flags in `mpconfigport.mk`


## ioctl Signature Bug Fix

During UART implementation, discovered and fixed a critical bug in CircuitPython's stream protocol affecting 8+ modules. See [IOCTL_FIX_DOCUMENTATION.md](IOCTL_FIX_DOCUMENTATION.md) for details.

**Fixed files:**
- `shared-bindings/busio/UART.c` - Changed `mp_uint_t arg` → `uintptr_t arg`
- `py/stream.h` - Added documentation explaining why `uintptr_t` is mandatory

**Impact:** Enables UART and improves overall CircuitPython correctness on non-ARM architectures.
