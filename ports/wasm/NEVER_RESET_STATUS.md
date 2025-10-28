# never_reset Implementation Status

## Overview

This document tracks the implementation status of `never_reset` functionality across all common-hal modules in the CircuitPython WASM port.

## What is never_reset?

The `never_reset` mechanism prevents supervisor-managed peripherals from being reset during soft resets (Ctrl+D in REPL). This is critical for:
- **Displays** - Maintaining displayio connections
- **Supervisor console** - Preserving UART debugging output
- **System PWM** - Backlight control, fans
- **System GPIOs** - Status LEDs, enable pins for critical hardware

## Why It Matters for WASM

The WASM port **supports soft resets** via Ctrl+D, making `never_reset` essential for maintaining system state across REPL sessions.

## Implementation Status by Module

### ✅ Fully Implemented

#### busio/I2C
**File:** [common-hal/busio/I2C.c](common-hal/busio/I2C.c)
**Status:** Complete implementation

- ✅ `bool never_reset` field in `i2c_bus_state_t` structure
- ✅ Reset function skips buses where `never_reset == true`
- ✅ Initialize `never_reset = false` in construct
- ✅ Full implementation of `common_hal_busio_i2c_never_reset()`
- ✅ Marks both bus and pins as never_reset

**Use case:** I2C displays managed by displayio

#### busio/UART
**File:** [common-hal/busio/UART.c](common-hal/busio/UART.c)
**Status:** Complete implementation

- ✅ `bool never_reset` field in `uart_port_state_t` structure
- ✅ Reset function skips ports where `never_reset == true`
- ✅ Initialize `never_reset = false` in construct
- ✅ Full implementation of `common_hal_busio_uart_never_reset()`
- ✅ Marks both port and pins as never_reset

**Use case:** Supervisor console UART

### ⚠️ Needs Implementation

#### pwmio/PWMOut
**File:** [common-hal/pwmio/PWMOut.c](common-hal/pwmio/PWMOut.c)
**Status:** Stub only

**Current state:**
```c
void common_hal_pwmio_pwmout_never_reset(pwmio_pwmout_obj_t *self) {
    // No-op for WASM
}
```

**Infrastructure available:**
- ✅ Has `pwm_state[64]` state array
- ✅ Has `pwmio_reset_pwm_state()` reset function
- ✅ Uses pin number as index
- ✅ Has `enabled` flag in structure
- ❌ Missing `never_reset` field in `pwm_state_t`
- ❌ Reset function doesn't check never_reset
- ❌ Never_reset function not implemented

**Structure (current):**
```c
typedef struct {
    uint16_t duty_cycle;
    uint32_t frequency;
    bool variable_freq;
    bool enabled;
    // Missing: bool never_reset;
} pwm_state_t;
```

**Use cases:**
- Display backlight control (supervisor-managed)
- Fan control for system cooling
- System indicator LEDs with PWM dimming
- Audio/buzzer control for system alerts

**Implementation needed:**
1. Add `bool never_reset` field to `pwm_state_t` structure (line 17)
2. Update `pwmio_reset_pwm_state()` to skip channels where `never_reset == true`
3. Initialize `never_reset = false` in construct function
4. Implement `common_hal_pwmio_pwmout_never_reset()` following I2C/UART pattern

#### digitalio/DigitalInOut
**File:** [common-hal/digitalio/DigitalInOut.c](common-hal/digitalio/DigitalInOut.c)
**Status:** Stub only

**Current state:**
```c
void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    // No-op for WASM
}
```

**Infrastructure available:**
- ✅ Has `gpio_state[64]` state array
- ✅ Has `digitalio_reset_gpio_state()` reset function
- ✅ Uses pin number as index
- ✅ Has `enabled` flag in structure
- ❌ Missing `never_reset` field in `gpio_pin_state_t`
- ❌ Reset function doesn't check never_reset
- ❌ Never_reset function not implemented

**Structure (current):**
```c
typedef struct {
    bool value;          // Current pin value
    uint8_t direction;   // 0=input, 1=output
    uint8_t pull;        // 0=none, 1=up, 2=down
    bool open_drain;     // Open-drain output mode
    bool enabled;        // Pin is enabled/in-use
    // Missing: bool never_reset;
} gpio_pin_state_t;
```

**Use cases:**
- Status LED control (system health indicators)
- Display enable/reset pins (displayio-managed)
- Power control pins for peripherals
- Chip select for supervisor-managed SPI devices
- Interrupt lines from system-critical devices

**Implementation needed:**
1. Add `bool never_reset` field to `gpio_pin_state_t` structure (line 19)
2. Update `digitalio_reset_gpio_state()` to skip pins where `never_reset == true`
3. Initialize `never_reset = false` in construct function
4. Implement `common_hal_digitalio_digitalinout_never_reset()` following I2C/UART pattern

**Note:** This is different from `never_reset_pin_number()` which marks the pin itself. This marks the DigitalInOut *object* managing the pin, so both need to be called.

### ❌ Not Yet Implemented

#### busio/SPI
**File:** [common-hal/busio/SPI.c](common-hal/busio/SPI.c)
**Status:** Module not implemented (stubs only)

**Current state:**
```c
void common_hal_busio_spi_never_reset(busio_spi_obj_t *self) {}
```

**Comment in file:**
```c
// SPI stub for WASM port - not yet implemented with new architecture
// TODO: Implement using direct state arrays like GPIO/Analog
```

**When SPI is implemented:**
- Will need state array following I2C/UART pattern
- Should include `never_reset` from the start
- Use cases: displayio SPI displays, SD cards, external flash

**Priority:** Implement when SPI module is developed

### ✅ Already Works Correctly

#### microcontroller/Pin
**File:** [common-hal/microcontroller/Pin.c](common-hal/microcontroller/Pin.c)
**Status:** Complete implementation

**Functions:**
- ✅ `never_reset_pin_number()` - Marks individual pins as never_reset
- ✅ `reset_all_pins()` - Skips pins where `never_reset == true`
- ✅ Used by I2C/UART implementations to mark their pins

**Note:** This is pin-level never_reset, separate from peripheral-level never_reset.

## Implementation Pattern

Based on I2C and UART implementations, the pattern is:

### 1. Add never_reset field to state structure

```c
typedef struct {
    // ... existing fields ...
    bool never_reset;  // If true, don't reset during soft reset
} module_state_t;
```

### 2. Update reset function to skip never_reset items

```c
void module_reset_state(void) {
    for (int i = 0; i < MAX_ITEMS; i++) {
        // Skip items marked as never_reset
        if (state_array[i].never_reset) {
            continue;
        }

        // Reset all other state
        state_array[i].field1 = default_value;
        state_array[i].enabled = false;
        // ... reset other fields ...
    }
}
```

### 3. Initialize never_reset = false in construct

```c
void common_hal_module_construct(...) {
    // ... existing construction code ...
    state_array[index].never_reset = false;
    // ... rest of construction ...
}
```

### 4. Implement never_reset function

```c
void common_hal_module_never_reset(module_obj_t *self) {
    // Mark this peripheral as never_reset
    uint8_t pin = self->pin->number;  // Or find by pins

    int8_t idx = find_peripheral_index(pin);
    if (idx >= 0) {
        state_array[idx].never_reset = true;

        // Also mark the pins as never_reset
        if (self->pin != NULL) {
            never_reset_pin_number(self->pin->number);
        }
        // ... mark other pins if applicable ...
    }
}
```

## Priority Recommendations

### High Priority
1. **pwmio/PWMOut** - Critical for display backlight and system indicators
2. **digitalio/DigitalInOut** - Critical for display control pins and status LEDs

### Medium Priority
3. **busio/SPI** - When SPI module is implemented, include never_reset from the start

### Rationale

PWMOut and DigitalInOut are already fully implemented modules that are likely to be used by displayio and supervisor. Implementing never_reset now prevents future bugs where displays lose configuration or system LEDs change state during soft resets.

## Testing Strategy

For each implementation:
1. Create peripheral marked as never_reset
2. Create second peripheral NOT marked as never_reset
3. Trigger soft reset (if available)
4. Verify first peripheral persists, second resets
5. Verify pins are properly marked

## Related Documentation

- [NEVER_RESET_IMPLEMENTATION.md](NEVER_RESET_IMPLEMENTATION.md) - Detailed documentation of I2C/UART implementation
- [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) - Overall WASM port implementation status

## Author

CircuitPython WASM Port Development (2025)
