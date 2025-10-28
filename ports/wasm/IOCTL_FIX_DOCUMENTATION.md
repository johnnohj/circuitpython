# ioctl Signature Bug Fix for CircuitPython

## Summary

Fixed a type signature bug in the stream protocol `ioctl` function that affects multiple CircuitPython modules. The bug prevented compilation on architectures with strict type checking (e.g., WASM/Emscripten) while remaining undetected on typical ARM platforms.

## The Bug

### Incorrect Signature (widely used in shared-bindings)
```c
static mp_uint_t module_ioctl(mp_obj_t self_in, mp_uint_t request, mp_uint_t arg, int *errcode)
```

### Correct Signature (defined in py/stream.h:79)
```c
mp_uint_t (*ioctl)(mp_obj_t obj, mp_uint_t request, uintptr_t arg, int *errcode)
```

### The Problem
- Third parameter should be `uintptr_t`, not `mp_uint_t`
- `uintptr_t` is required because `arg` may hold either integer values OR pointer values
- On 32-bit ARM (most CircuitPython boards), both types are 32-bit so compilers don't complain
- On WASM and other architectures, strict type checking catches the incompatibility

## Files Fixed

### 1. `shared-bindings/busio/UART.c` (line 280)
**Before:**
```c
static mp_uint_t busio_uart_ioctl(mp_obj_t self_in, mp_uint_t request, mp_uint_t arg, int *errcode) {
    // ...
    mp_uint_t flags = arg;
```

**After:**
```c
static mp_uint_t busio_uart_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    // ...
    uintptr_t flags = arg;
```

### 2. `py/stream.h` (line 79)
Added documentation comment explaining why `uintptr_t` is mandatory.

## Other Affected Modules

The following modules in `shared-bindings/` have the same bug (NOT fixed in this patch):
- `terminalio/Terminal.c`
- `_bleio/CharacteristicBuffer.c`
- `ssl/SSLSocket.c`
- `usb_cdc/Serial.c`
- `socketpool/Socket.c`
- `usb_midi/PortOut.c`
- `usb_midi/PortIn.c`

**Correctly implemented:**
- `keypad/EventQueue.c` ✓

## Why This Matters

### Correctness
On platforms where `sizeof(void*) > sizeof(unsigned int)`:
- Using `mp_uint_t` instead of `uintptr_t` can truncate pointer values
- This would cause memory corruption bugs

### Portability
- WASM/Emscripten enforces strict type compatibility
- Future architectures may have different pointer sizes
- Following the protocol definition ensures all implementations work

### Standard Compliance
The `mp_stream_p_t` protocol in `py/stream.h` explicitly defines `uintptr_t` as the correct type. All implementations should match this definition.

## Testing

Successfully tested on WASM port:
- UART creation with TX/RX pins ✓
- Baudrate modification ✓
- Timeout configuration ✓
- Write operations ✓
- RX buffer operations ✓
- TX-only and RX-only modes ✓
- Deinit ✓

## Recommendation for Upstream

1. **Immediate:** Fix `busio/UART.c` (this patch)
2. **Follow-up:** Audit and fix all other affected modules listed above
3. **Long-term:** Consider adding a compiler warning or static analysis to catch this pattern

## Technical Details

### Type Definitions (32-bit WASM)
```c
typedef unsigned int mp_uint_t;        // 32-bit
typedef unsigned long uintptr_t;       // 32-bit (matches pointer size)
```

While both are 32-bit on WASM32, C treats them as distinct types. The compiler error:
```
error: incompatible function pointer types initializing
'mp_uint_t (*)(mp_obj_t, mp_uint_t, uintptr_t, int *)' with an expression of type
'mp_uint_t (mp_obj_t, mp_uint_t, mp_uint_t, int *)'
```

### Why ARM Doesn't Catch This
On ARM GCC, when both types have the same size and representation, the compiler often treats them as compatible for function pointer assignment, even though they're technically different types in C.

## Author
WASM Port Development Team
Discovered during CircuitPython WASM port implementation (2025)
