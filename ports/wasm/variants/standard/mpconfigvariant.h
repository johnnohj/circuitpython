// CircuitPython WebAssembly Standard Variant
// MicroPython base + CircuitPython APIs + HAL providers

// Enable JavaScript FFI for hardware provider communication
#define MICROPY_VARIANT_ENABLE_JS_HOOK (1)

// Enable HAL provider system (matches mpconfigport.h)
#define CIRCUITPY_HAL_PROVIDER (1)

// Standard build features
#define MICROPY_PY_SYS_PLATFORM "webassembly"
