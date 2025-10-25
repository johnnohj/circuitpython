# Naming Clarification: Two Separate Virtual Hardware Systems

## The Confusion

The WASM port has TWO separate "virtual hardware" systems that were using confusingly similar names:

1. **Timing hardware** (clock/crystal simulation) - was called `virtual_hardware_t`
2. **I/O hardware** (GPIO/analog pins) - in file named `virtual_hardware.c`

This caused confusion about which "virtual hardware" was being referenced.

## The Solution

We've renamed the timing-related structures for clarity:

### Before (Confusing)

```c
// In shared_memory.h - TIMING hardware
typedef struct {
    volatile uint64_t ticks_32khz;
    // ...
} virtual_hardware_t;  // ❌ Confusing name

extern volatile virtual_hardware_t virtual_hardware;
void* get_virtual_hardware_ptr(void);
```

```c
// In virtual_hardware.c - I/O hardware
static gpio_state_t gpio_pins[64];  // ❌ Same "virtual_hardware" prefix
```

### After (Clear)

```c
// In shared_memory.h - TIMING hardware (renamed)
typedef struct {
    volatile uint64_t ticks_32khz;
    // ...
} virtual_clock_hw_t;  // ✅ Clear: this is CLOCK hardware

extern volatile virtual_clock_hw_t virtual_clock_hw;
void* get_virtual_clock_hw_ptr(void);
```

```c
// In virtual_hardware.c - I/O hardware (name makes sense now)
static gpio_state_t gpio_pins[64];  // ✅ Separate from clock hardware
```

## Two Distinct Systems

### 1. Virtual Clock Hardware (Timing)

**File**: [shared_memory.c](shared_memory.c) / [shared_memory.h](shared_memory.h)

**Purpose**: Simulates the hardware crystal oscillator / timing system

**What it does**:
- JavaScript writes tick counts
- WASM reads ticks instantly (no yielding)
- Powers `time.sleep()`, `time.monotonic()`, `supervisor.ticks_ms()`

**Key functions**:
- `get_virtual_clock_hw_ptr()` - Returns pointer for JavaScript
- `read_virtual_ticks_32khz()` - WASM reads current time
- `get_time_mode()` - Check if realtime/manual/fast-forward

**JavaScript integration**:
```javascript
const clockPtr = Module._get_virtual_clock_hw_ptr();
virtualClock = new VirtualClock(wasmInstance, wasmMemory);
virtualClock.startRealtime();  // Increments ticks_32khz
```

### 2. Virtual I/O Hardware (GPIO/Analog/PWM/etc.)

**File**: [virtual_hardware.c](virtual_hardware.c) / [virtual_hardware.h](virtual_hardware.h)

**Purpose**: Simulates GPIO pins, analog I/O, and other peripherals

**What it does**:
- Stores pin state (direction, pull, value, open_drain)
- Powers `digitalio`, `analogio`, `pwmio` modules
- JavaScript can observe outputs and inject inputs

**Key functions**:
- `virtual_gpio_set_value()` - Python writes to pin
- `virtual_gpio_get_value()` - Python reads from pin
- `virtual_gpio_get_output_value()` - JavaScript reads LED state
- `virtual_gpio_set_input_value()` - JavaScript simulates button press

**JavaScript integration**:
```javascript
// Read LED state for visualization
const ledState = ctpy._virtual_gpio_get_output_value(13);

// Simulate button press
ctpy._virtual_gpio_set_input_value(2, false);
```

## Summary of Changes

### Files Modified

1. **[shared_memory.h](shared_memory.h)**
   - `virtual_hardware_t` → `virtual_clock_hw_t`
   - `virtual_hardware` → `virtual_clock_hw`
   - `get_virtual_hardware_ptr()` → `get_virtual_clock_hw_ptr()`

2. **[shared_memory.c](shared_memory.c)**
   - Updated struct name and function name
   - Added clarifying comments

3. **[supervisor/port.c](supervisor/port.c)**
   - Updated comment: `virtual_hardware.ticks_32khz` → `virtual_clock_hw.ticks_32khz`
   - Updated code: `virtual_hardware.wasm_yields_count++` → `virtual_clock_hw.wasm_yields_count++`

4. **[api.js](api.js)**
   - `Module._get_virtual_hardware_ptr()` → `Module._get_virtual_clock_hw_ptr()`
   - Updated variable names and comments

5. **[Makefile](Makefile)**
   - `_get_virtual_hardware_ptr` → `_get_virtual_clock_hw_ptr` in exports

### Test Results

All tests pass after renaming:

```bash
$ node build-standard/circuitpython.mjs /tmp/gpio_test.py
CircuitPython WASM - GPIO Test
================================
LED initialized as OUTPUT
Set LED = True, read back: True
Set LED = False, read back: False
Button value (PULL_UP): True
Button value (PULL_DOWN): False

Test completed successfully!
```

## Architecture Diagram

```
┌─────────────────────────────────────────────────┐
│  Virtual Clock Hardware (Timing)                │
│  shared_memory.c / virtual_clock_hw_t           │
│                                                 │
│  - ticks_32khz (JavaScript writes, WASM reads)  │
│  - cpu_frequency_hz                             │
│  - time_mode (realtime/manual/fast-forward)     │
│                                                 │
│  Powers: time.sleep(), time.monotonic()         │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│  Virtual I/O Hardware (GPIO/Analog/PWM)         │
│  virtual_hardware.c / gpio_state_t              │
│                                                 │
│  - gpio_pins[64] (direction, pull, value, etc.) │
│  - analog_pins[64] (value, is_output, enabled)  │
│                                                 │
│  Powers: digitalio, analogio, pwmio             │
│  JS Interface: observe outputs, inject inputs   │
└─────────────────────────────────────────────────┘
```

## Benefits of Clarification

✅ **Clear naming** - No confusion about which "virtual hardware" is being referenced
✅ **Distinct purposes** - Clock vs I/O hardware are obviously separate
✅ **Easier maintenance** - New developers immediately understand the split
✅ **Extensible** - Can add more virtual hardware types without naming conflicts

## For Future Reference

When adding new virtual hardware systems (e.g., display, audio), use clear, descriptive names:

- `virtual_display_hw_t` for framebuffer/display
- `virtual_audio_hw_t` for audio output
- `virtual_spi_hw_t` for SPI bus state
- etc.

Avoid generic "virtual_hardware" - be specific about **what kind** of hardware!

---

**Status**: Renaming complete, all tests passing, no functionality changes
