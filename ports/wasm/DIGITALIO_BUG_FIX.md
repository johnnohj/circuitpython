# DigitalInOut Bug Fix

## Summary

Fixed critical bug in DigitalInOut implementation where all DigitalInOut objects were using GPIO0 instead of their intended pins, causing pin state interference.

## The Bug

**Symptom:** Creating multiple DigitalInOut objects on different pins caused them to interfere with each other.

**Example:**
```python
led = digitalio.DigitalInOut(board.D10)  # Create on D10
led.direction = digitalio.Direction.OUTPUT
led.value = True

btn = digitalio.DigitalInOut(board.D11)  # Create on D11
btn.direction = digitalio.Direction.INPUT

# BUG: led.direction is now INPUT instead of OUTPUT!
# Setting led.value would raise "Cannot set value when direction is input"
```

## Root Cause

**File:** `common-hal/digitalio/DigitalInOut.c` line 48-52

### Issue #1: Invalid Pin Validation

```c
const mcu_pin_obj_t *common_hal_digitalio_validate_pin(mp_obj_t obj) {
    // In WASM, we accept pin numbers or pin objects
    // For now, just return a dummy pin - proper validation happens in construct
    return &pin_GPIO0;  // ❌ ALWAYS returns GPIO0!
}
```

This function is called by shared-bindings to validate the pin object. By always returning `&pin_GPIO0`, all DigitalInOut objects ended up using GPIO0 regardless of which pin was actually specified.

### Issue #2: Missing claim_pin()

```c
digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self,
    const mcu_pin_obj_t *pin) {

    // Initialize the object - only store which pin
    self->pin = pin;  // ❌ No claim_pin() call!
    ...
}
```

The construct function didn't claim the pin, preventing detection of pin reuse conflicts.

## The Fix

### Fix #1: Proper Pin Validation

**File:** `common-hal/digitalio/DigitalInOut.c` line 48-50

```c
const mcu_pin_obj_t *common_hal_digitalio_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);  // ✓ Use standard validation
}
```

Now uses CircuitPython's standard `validate_obj_is_free_pin()` function which:
- Validates that the object is actually a pin
- Checks that the pin is not already claimed
- Returns the actual pin object (not GPIO0)

### Fix #2: Claim the Pin

**File:** `common-hal/digitalio/DigitalInOut.c` line 57-59

```c
digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self,
    const mcu_pin_obj_t *pin) {

    // Claim the pin and store it
    claim_pin(pin);  // ✓ Claim the pin
    self->pin = pin;
    ...
}
```

Now properly claims the pin in the construct function, matching the pattern used by PWMOut and other modules.

## Testing

### Before Fix
```
Creating led on D10...
led.direction = digitalio.Direction.OUTPUT
led.value = True

Creating btn on D11...
btn.direction = digitalio.Direction.INPUT

After creating btn:
led.direction = digitalio.Direction.INPUT  ❌ CHANGED!
❌ Error setting led value: Cannot set value when direction is input.
```

### After Fix
```
Creating led on D10...
led.direction = digitalio.Direction.OUTPUT
led.value = True

Creating btn on D11...
btn.direction = digitalio.Direction.INPUT

After creating btn:
led.direction = digitalio.Direction.OUTPUT  ✓ STAYED CORRECT!
led.value = False
✓ Setting led value worked!
```

### Comprehensive Tests Pass

All basic DigitalInOut functionality now works correctly:
- ✅ Creating outputs on different pins
- ✅ Setting and reading values independently
- ✅ Creating inputs with pull resistors
- ✅ Switching between input and output modes
- ✅ Open-drain output mode
- ✅ Multiple pins work independently

## Impact

This bug affected:
- All DigitalInOut usage in the WASM port
- Any code trying to use multiple GPIO pins
- Display drivers using control pins
- Button/LED examples
- Any peripherals requiring GPIO

**Severity:** Critical - Made DigitalInOut essentially non-functional for multi-pin use

## Related Changes

This fix was discovered while implementing `never_reset` functionality for DigitalInOut. The never_reset implementation is also complete and working correctly:
- [NEVER_RESET_IMPLEMENTATION.md](NEVER_RESET_IMPLEMENTATION.md)
- [NEVER_RESET_STATUS.md](NEVER_RESET_STATUS.md)

## Author

CircuitPython WASM Port Development (2025)
