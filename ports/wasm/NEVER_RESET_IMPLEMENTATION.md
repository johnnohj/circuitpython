# never_reset Implementation for WASM Port

## Summary

Properly implemented `never_reset` functionality for busio modules (I2C and UART) in the CircuitPython WASM port. This ensures that peripherals used by the supervisor, displays, or other system components persist across soft resets (Ctrl+D in REPL).

## Why This Matters for WASM

While WASM doesn't have physical hardware, the CircuitPython WASM port **does support soft resets** when users press Ctrl+D in the REPL or when code.py finishes execution. Without proper `never_reset` implementation:

- Displays would lose their configuration
- Supervisor console UART would be disconnected
- I2C peripherals would need reinitialization
- State managed by displayio would be lost

This is **not** just a hardware concern - it's a core part of CircuitPython's execution model that applies to WASM as well.

## Implementation Details

### I2C (busio.I2C)

**File:** `common-hal/busio/I2C.c`

**Changes:**
1. Added `bool never_reset` field to `i2c_bus_state_t` structure (line 36)
2. Updated `busio_reset_i2c_state()` to skip buses where `never_reset == true` (lines 61-64)
3. Initialize `never_reset = false` in construct function (line 130)
4. Implemented `common_hal_busio_i2c_never_reset()` to mark bus and pins (lines 316-334)

**Key code:**
```c
void common_hal_busio_i2c_never_reset(busio_i2c_obj_t *self) {
    // Mark this I2C bus as never_reset so it persists across soft resets
    // This is important for displays and other supervisor-managed peripherals
    uint8_t scl_pin = (self->scl != NULL) ? self->scl->number : 0xFF;
    uint8_t sda_pin = (self->sda != NULL) ? self->sda->number : 0xFF;

    int8_t bus_idx = find_i2c_bus(scl_pin, sda_pin);
    if (bus_idx >= 0) {
        i2c_buses[bus_idx].never_reset = true;

        // Also mark the pins as never_reset
        if (self->scl != NULL) {
            never_reset_pin_number(self->scl->number);
        }
        if (self->sda != NULL) {
            never_reset_pin_number(self->sda->number);
        }
    }
}
```

### UART (busio.UART)

**File:** `common-hal/busio/UART.c`

**Changes:**
1. Added `bool never_reset` field to `uart_port_state_t` structure (line 33)
2. Updated `busio_reset_uart_state()` to skip ports where `never_reset == true` (lines 57-60)
3. Initialize `never_reset = false` in construct function (line 167)
4. Implemented `common_hal_busio_uart_never_reset()` to mark port and pins (lines 333-351)

**Key code:**
```c
void common_hal_busio_uart_never_reset(busio_uart_obj_t *self) {
    // Mark this UART port as never_reset so it persists across soft resets
    // This is important for supervisor console and other system-managed UARTs
    uint8_t tx_pin = (self->tx != NULL) ? self->tx->number : 0xFF;
    uint8_t rx_pin = (self->rx != NULL) ? self->rx->number : 0xFF;

    int8_t port_idx = find_uart_port(tx_pin, rx_pin);
    if (port_idx >= 0) {
        uart_ports[port_idx].never_reset = true;

        // Also mark the pins as never_reset
        if (self->tx != NULL) {
            never_reset_pin_number(self->tx->number);
        }
        if (self->rx != NULL) {
            never_reset_pin_number(self->rx->number);
        }
    }
}
```

## Reset Behavior

### Before never_reset Implementation
```c
void busio_reset_uart_state(void) {
    for (int i = 0; i < MAX_UART_PORTS; i++) {
        // Reset ALL ports indiscriminately
        uart_ports[i].enabled = false;
        // ... reset all state
    }
}
```

### After never_reset Implementation
```c
void busio_reset_uart_state(void) {
    for (int i = 0; i < MAX_UART_PORTS; i++) {
        // Skip ports marked as never_reset (e.g., used by supervisor console)
        if (uart_ports[i].never_reset) {
            continue;
        }

        // Only reset ports not marked as never_reset
        uart_ports[i].enabled = false;
        // ... reset all state
    }
}
```

## Pin Management

When a peripheral is marked as `never_reset`, both the peripheral state **and its pins** are preserved:

```c
// Mark the peripheral as never_reset
uart_ports[port_idx].never_reset = true;

// Also mark the pins as never_reset so they can't be claimed by other code
if (self->tx != NULL) {
    never_reset_pin_number(self->tx->number);
}
if (self->rx != NULL) {
    never_reset_pin_number(self->rx->number);
}
```

This prevents user code from accidentally claiming pins used by supervisor-managed peripherals.

## Testing

**Test file:** `/tmp/test_uart_never_reset.py`

Tests verify:
- ✅ UART creation and basic functionality
- ✅ Multiple UART instances with different configurations
- ✅ Baudrate and timeout modifications
- ✅ Write operations
- ✅ Buffer management
- ✅ Proper deinitialization

**All tests pass successfully.**

## Use Cases

### Supervisor Console
The supervisor may use a UART for debugging output that should persist across soft resets:
```c
// In supervisor initialization:
uart_console = busio_uart_construct(...);
common_hal_busio_uart_never_reset(uart_console);
```

### Display Management (displayio)
Displays using I2C need to maintain their connection:
```c
// When displayio initializes a display:
display_i2c = busio_i2c_construct(...);
common_hal_busio_i2c_never_reset(display_i2c);
```

### System Peripherals
Any peripheral managed by the supervisor or core system should be marked as `never_reset` to maintain state across user code executions.

## Soft Reset Flow

1. User presses Ctrl+D or code.py finishes
2. CircuitPython calls `reset_port()` (in supervisor)
3. `reset_port()` calls `busio_reset_i2c_state()` and `busio_reset_uart_state()`
4. Reset functions skip peripherals where `never_reset == true`
5. Supervisor-managed peripherals remain active
6. New user code starts with clean slate but system peripherals intact

## Comparison with Hardware Ports

This implementation matches the behavior of hardware ports like ESP32, RP2040, etc.:
- Peripherals start with `never_reset = false`
- Supervisor marks critical peripherals as `never_reset = true` during initialization
- Soft resets preserve `never_reset` peripherals
- Hard resets clear everything (VM restart in WASM case)

## Future Work

Consider implementing `never_reset` for:
- PWMOut channels (for system-managed PWM)
- SPI buses (if SPI implementation is added)
- Other busio peripherals as they're implemented

## Author

CircuitPython WASM Port Development (2025)

Implemented as part of the comprehensive WASM port effort to ensure full compatibility with CircuitPython's execution model and supervisor integration.
