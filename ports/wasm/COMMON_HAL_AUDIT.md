# common_hal Function Implementation Audit

## Overview

This document audits the implementation of `common_hal_*` functions in the CircuitPython WASM port for digitalio and microcontroller modules, identifying gaps and misimplementations from early development.

## Module: digitalio/DigitalInOut

### Required Functions (from shared-bindings/digitalio/DigitalInOut.h)

| Function | Status | Notes |
|----------|--------|-------|
| `common_hal_digitalio_validate_pin()` | ✅ **FIXED** | Was returning hardcoded `&pin_GPIO0`, now uses `validate_obj_is_free_pin()` |
| `common_hal_digitalio_digitalinout_construct()` | ✅ **FIXED** | Was missing `claim_pin()` call, now added |
| `common_hal_digitalio_digitalinout_deinit()` | ✅ Implemented | Correctly releases pin and resets state |
| `common_hal_digitalio_digitalinout_deinited()` | ✅ Implemented | Checks if `self->pin == NULL` |
| `common_hal_digitalio_digitalinout_switch_to_input()` | ✅ Implemented | Sets direction and pull resistors |
| `common_hal_digitalio_digitalinout_switch_to_output()` | ✅ Implemented | Sets direction, value, and drive mode |
| `common_hal_digitalio_digitalinout_get_direction()` | ✅ Implemented | Returns INPUT or OUTPUT |
| `common_hal_digitalio_digitalinout_set_value()` | ✅ Implemented | Sets output value |
| `common_hal_digitalio_digitalinout_get_value()` | ✅ Implemented | Gets value with pull resistor simulation |
| `common_hal_digitalio_digitalinout_set_drive_mode()` | ✅ Implemented | Sets push-pull or open-drain |
| `common_hal_digitalio_digitalinout_get_drive_mode()` | ✅ Implemented | Gets current drive mode |
| `common_hal_digitalio_digitalinout_set_pull()` | ✅ Implemented | Sets pull resistor |
| `common_hal_digitalio_digitalinout_get_pull()` | ✅ Implemented | Gets current pull setting |
| `common_hal_digitalio_digitalinout_never_reset()` | ✅ **NEWLY IMPLEMENTED** | Marks GPIO state and pin as never_reset |
| `common_hal_digitalio_digitalinout_get_reg()` | ✅ Implemented | Returns NULL (not supported in WASM) |
| `common_hal_digitalio_has_reg_op()` | ✅ Implemented | Returns false (not supported in WASM) |

### Bugs Fixed

#### Bug #1: validate_pin() Always Returned GPIO0
**File:** `common-hal/digitalio/DigitalInOut.c` line 48-50

**Before:**
```c
const mcu_pin_obj_t *common_hal_digitalio_validate_pin(mp_obj_t obj) {
    return &pin_GPIO0;  // ❌ WRONG!
}
```

**After:**
```c
const mcu_pin_obj_t *common_hal_digitalio_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);  // ✅ CORRECT
}
```

**Impact:** Critical - All DigitalInOut objects were using GPIO0, causing pin interference.

#### Bug #2: Missing claim_pin() Call
**File:** `common-hal/digitalio/DigitalInOut.c` line 57-59

**Before:**
```c
digitalinout_result_t common_hal_digitalio_digitalinout_construct(...) {
    self->pin = pin;  // ❌ No claim!
    ...
}
```

**After:**
```c
digitalinout_result_t common_hal_digitalio_digitalinout_construct(...) {
    claim_pin(pin);  // ✅ Claim the pin
    self->pin = pin;
    ...
}
```

**Impact:** Medium - Pin reuse conflicts not detected.

## Module: microcontroller/Pin

### Required Functions (from shared-bindings/microcontroller/Pin.h)

| Function | Status | Notes |
|----------|--------|-------|
| `common_hal_mcu_pin_is_free()` | ✅ Implemented | Checks `!pin->claimed` |
| `common_hal_never_reset_pin()` | ✅ Implemented | Calls `never_reset_pin_number()` |
| `common_hal_reset_pin()` | ✅ Implemented | Calls `reset_pin_number()` |
| `common_hal_mcu_pin_number()` | ✅ Implemented | Returns `pin->number` |
| `common_hal_mcu_pin_claim()` | ✅ Implemented | Calls `claim_pin()` |
| `common_hal_mcu_pin_claim_number()` | ✅ **NEWLY IMPLEMENTED** | Claims pin by number |
| `common_hal_mcu_pin_reset_number()` | ✅ Implemented | Calls `reset_pin_number()` |

### Implementation Complete

All required microcontroller/Pin functions are now implemented, including the previously missing `common_hal_mcu_pin_claim_number()`.

**Implementation added:**
```c
void common_hal_mcu_pin_claim_number(uint8_t pin_no) {
    if (pin_no >= 64) {
        return;
    }
    all_pins[pin_no]->claimed = true;
}
```

**File:** `common-hal/microcontroller/Pin.c` line 252-257

## Module: analogio/AnalogIn

### Required Functions (from shared-bindings/analogio/AnalogIn.h)

| Function | Status | Notes |
|----------|--------|-------|
| `common_hal_analogio_analogin_validate_pin()` | ✅ **NEWLY ADDED** | Was completely missing |
| `common_hal_analogio_analogin_construct()` | ✅ Implemented | Properly calls `claim_pin()` |
| `common_hal_analogio_analogin_deinit()` | ✅ Implemented | Correctly releases pin and resets state |
| `common_hal_analogio_analogin_deinited()` | ✅ Implemented | Checks if `self->pin == NULL` |
| `common_hal_analogio_analogin_get_value()` | ✅ Implemented | Returns ADC value from state array |
| `common_hal_analogio_analogin_get_reference_voltage()` | ✅ Implemented | Returns 3.3V reference |

### Missing Implementation (Fixed)

#### Missing: common_hal_analogio_analogin_validate_pin()

This function was completely absent from AnalogIn.c.

**Added implementation:**
```c
const mcu_pin_obj_t *common_hal_analogio_analogin_validate_pin(mp_obj_t obj) {
    return validate_obj_is_free_pin(obj, MP_QSTR_pin);
}
```

**File:** `common-hal/analogio/AnalogIn.c` line 38-40

**Impact:** Without this function, pin validation wasn't working properly. Similar pattern to the digitalio bug.

## Module: analogio/AnalogOut

### Required Functions (from shared-bindings/analogio/AnalogOut.h)

| Function | Status | Notes |
|----------|--------|-------|
| `common_hal_analogio_analogout_construct()` | ✅ Implemented | Properly calls `claim_pin()` |
| `common_hal_analogio_analogout_deinit()` | ✅ Implemented | Correctly releases pin and resets state |
| `common_hal_analogio_analogout_deinited()` | ✅ Implemented | Checks if `self->pin == NULL` |
| `common_hal_analogio_analogout_set_value()` | ✅ Implemented | Sets DAC value in state array |

**Note:** AnalogOut does not have a validate_pin function (validation happens in shared-bindings).

## Module: microcontroller/__init__

### Required Functions

Most microcontroller/__init__ functions are implemented correctly in `common-hal/microcontroller/__init__.c`:
- ✅ `common_hal_mcu_delay_us()`
- ✅ `common_hal_mcu_disable_interrupts()`
- ✅ `common_hal_mcu_enable_interrupts()`
- ✅ `common_hal_mcu_on_next_reset()`
- ✅ `common_hal_mcu_reset()`
- ✅ Various reset reason functions

No issues identified in this module.

## Summary of Issues Found

### Critical (Fixed)
1. ✅ **digitalio validate_pin()** - Was hardcoded to GPIO0
2. ✅ **digitalio construct()** - Missing claim_pin() call

### High Priority (Completed)
3. ✅ **digitalio never_reset()** - Was stub, now fully implemented
4. ✅ **pwmio never_reset()** - Was stub, now fully implemented

### Low Priority (Completed)
5. ✅ **microcontroller claim_number()** - Now implemented

## Recommendations

### Completed
- ✅ Fix digitalio validate_pin() to use proper validation
- ✅ Add claim_pin() call to digitalio construct()
- ✅ Implement never_reset for digitalio
- ✅ Implement never_reset for pwmio

### All Recommendations Completed
- ✅ All required common_hal functions now properly implemented

## Testing

All critical and high-priority issues have been fixed and tested:
- ✅ Multiple GPIO pins work independently
- ✅ Pin validation works correctly
- ✅ Pin claiming prevents conflicts
- ✅ never_reset preserves state across soft resets
- ✅ All existing functionality maintained

## Related Documentation

- [DIGITALIO_BUG_FIX.md](DIGITALIO_BUG_FIX.md) - Detailed analysis of digitalio bugs
- [NEVER_RESET_IMPLEMENTATION.md](NEVER_RESET_IMPLEMENTATION.md) - I2C/UART never_reset details
- [NEVER_RESET_STATUS.md](NEVER_RESET_STATUS.md) - Complete never_reset status across all modules

## Conclusion

The early digitalio, analogio, and microcontroller implementations had gaps and bugs from before the WASM port development team fully understood CircuitPython integration patterns. All identified issues have been resolved:

### Critical Bugs Fixed
1. ✅ **digitalio validate_pin() hardcoded to GPIO0** - All pins mapped to same hardware → Now uses proper validation
2. ✅ **digitalio Missing claim_pin() call** - Pin conflicts not detected → Now properly claims pins
3. ✅ **analogio validate_pin() completely missing** - Pin validation non-functional → Now properly implemented

### Improvements Implemented
4. ✅ **digitalio never_reset** - Stub implementation → Full functionality for GPIO preservation
5. ✅ **pwmio never_reset** - Stub implementation → Full functionality for PWM preservation
6. ✅ **microcontroller claim_number** - Missing function → Now implemented

The WASM port now correctly implements **ALL** required common_hal functions for digitalio, analogio, and microcontroller modules with proper CircuitPython integration patterns.

## Author

CircuitPython WASM Port Development (2025)
