# Library JavaScript Modules

The WASM port uses modular JavaScript files to bridge between C (common-hal) and JavaScript. Each module corresponds to a CircuitPython hardware module.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│  Python Code (import digitalio)                          │
└────────────────┬─────────────────────────────────────────┘
                 │
┌────────────────▼─────────────────────────────────────────┐
│  shared-bindings/digitalio/ (Python API)                 │
│  - Parses arguments                                       │
│  - Calls common_hal_* functions                           │
└────────────────┬─────────────────────────────────────────┘
                 │
┌────────────────▼─────────────────────────────────────────┐
│  common-hal/digitalio/ (Hardware Abstraction)            │
│  - gpio_state[] array = "virtual hardware"               │
│  - get_gpio_state_ptr() exposes to JavaScript            │
└────────────────┬─────────────────────────────────────────┘
                 │
┌────────────────▼─────────────────────────────────────────┐
│  library_digitalio.js (JavaScript Bridge)                │
│  - mp_js_gpio_get_value() reads gpio_state[]            │
│  - mp_js_gpio_set_input_value() writes gpio_state[]     │
└──────────────────────────────────────────────────────────┘
```

## Module Files

### library_supervisor.js
**Purpose:** Core supervisor timing and system functions

**Functions:**
- `mp_js_ticks_ms()` - Get milliseconds for time.monotonic()
- `mp_js_time_ms()` - Get absolute time
- `mp_js_hook()` - Node.js stdin handling
- `mp_js_random_u32()` - Cryptographic random

**State:** `virtual_clock_hw` in supervisor/port.c

### library_digitalio.js
**Purpose:** GPIO pin access (DigitalInOut)

**Functions:**
- `mp_js_gpio_get_value(pin)` - Read output pin state
- `mp_js_gpio_set_input_value(pin, value)` - Simulate button press
- `mp_js_gpio_get_direction(pin)` - Check if input/output

**State:** `gpio_state[]` in common-hal/digitalio/DigitalInOut.c

**JavaScript Usage:**
```javascript
// Read LED state for visualization
const led13_on = Module._mp_js_gpio_get_value(13);

// Simulate button press on D2
Module._mp_js_gpio_set_input_value(2, 1);  // Press
Module._mp_js_gpio_set_input_value(2, 0);  // Release
```

### library_analogio.js
**Purpose:** ADC/DAC access (AnalogIn, AnalogOut)

**Functions:**
- `mp_js_analog_get_value(pin)` - Read DAC output
- `mp_js_analog_set_input_value(pin, value)` - Simulate sensor reading

**State:** `analog_state[]` in common-hal/analogio/AnalogIn.c

**JavaScript Usage:**
```javascript
// Read DAC output for oscilloscope
const dac_value = Module._mp_js_analog_get_value(0);

// Simulate potentiometer at 50%
Module._mp_js_analog_set_input_value(1, 32768);  // 0-65535 range
```

## Adding New Modules

When implementing a new CircuitPython module (e.g., pwmio):

1. **Create common-hal implementation:**
   ```c
   // common-hal/pwmio/PWMOut.c
   pwm_state_t pwm_state[64];
   
   EMSCRIPTEN_KEEPALIVE
   pwm_state_t* get_pwm_state_ptr(void) {
       return pwm_state;
   }
   ```

2. **Create library module:**
   ```javascript
   // library_pwmio.js
   mergeInto(LibraryManager.library, {
       mp_js_pwm__postset: `
           var pwmStatePtr = Module.ccall('get_pwm_state_ptr', 'number', [], []);
       `,
       
       mp_js_pwm_get_duty: (pin) => {
           const view = new DataView(Module.HEAPU8.buffer, pwmStatePtr + pin * 8, 8);
           return view.getUint16(0, true);  // Read duty_cycle field
       },
   });
   ```

3. **Add to Makefile:**
   ```makefile
   SRC_LIB = \
       library_supervisor.js \
       library_digitalio.js \
       library_analogio.js \
       library_pwmio.js
   ```

4. **Export pointer in Makefile:**
   ```makefile
   EXPORTED_FUNCTIONS_EXTRA += _get_pwm_state_ptr,\
   ```

## Benefits of Modular Structure

1. **Matches common-hal organization** - Easy to find related code
2. **Conditional compilation** - Only include what's enabled
3. **Clear ownership** - Each module manages its own state access
4. **Easier maintenance** - Smaller files, focused purpose
5. **Future-ready** - Easy to add displayio, busio, etc.

## JavaScript Memory Access Pattern

All modules follow the same pattern:

```javascript
// 1. Get pointer to state array (once, in __postset)
var statePtr = Module.ccall('get_<module>_state_ptr', 'number', [], []);

// 2. Create DataView for specific pin/channel
function getStateView(index) {
    return new DataView(Module.HEAPU8.buffer, statePtr + index * STRUCT_SIZE, STRUCT_SIZE);
}

// 3. Read/write fields using DataView
const value = view.getUint16(OFFSET, true);  // little-endian
view.setUint16(OFFSET, newValue, true);
```

This gives JavaScript **direct memory access** to CircuitPython's virtual hardware state!

## Migration Notes

**Old monolithic library.js** → **Modular library_*.js**

The old `library.js` has been archived as `library.js.old`. All functionality has been split into thematic modules. The Makefile automatically includes all `SRC_LIB` files via `--js-library` flags.

No Python or C code changes needed - only the JavaScript bridge is reorganized.
