# CircuitPython WebAssembly Abstract Hardware Layer (HAL) Integration Guide

## Overview

The Abstract Hardware Layer (Layer 2) bridges the pure Python interpreter (Layer 1) with environment-specific hardware implementations (Layer 3). It provides hardware module APIs without implementation dependencies.

## Architecture Design

### Layer 2 (HAL) Responsibilities
- **Hardware module APIs**: digitalio, analogio, busio, board
- **Platform-agnostic interfaces**: Pin abstraction, protocol definitions
- **Provider registration**: Runtime hardware backend selection
- **Graceful degradation**: Fallback behaviors when hardware unavailable

### Key Design Principles
- **Provider Pattern**: Hardware implementation pluggable at runtime
- **Interface Contracts**: Stable APIs regardless of backend
- **Zero Implementation**: HAL defines interfaces, doesn't implement hardware
- **Environment Agnostic**: Works in browser, Node.js, embedded, or simulation

## Implementation Structure

### Directory Layout
```
variants/hal/
├── mpconfigvariant.mk         # HAL build configuration
├── mpconfigvariant.h          # HAL feature flags
├── hal_provider.h             # Hardware provider interface
├── hal_provider.c             # Provider registration system
├── hal_pin.h                  # Pin abstraction definitions
├── hal_pin.c                  # Pin management implementation
└── README.md                  # HAL documentation
```

### Core Interface Definitions

#### hal_provider.h - Provider Interface
```c
#ifndef HAL_PROVIDER_H
#define HAL_PROVIDER_H

#include "py/obj.h"

// Hardware provider capability flags
typedef enum {
    HAL_CAP_DIGITAL_IO = (1 << 0),
    HAL_CAP_ANALOG_IN = (1 << 1),
    HAL_CAP_ANALOG_OUT = (1 << 2),
    HAL_CAP_I2C = (1 << 3),
    HAL_CAP_SPI = (1 << 4),
    HAL_CAP_UART = (1 << 5),
    HAL_CAP_PWM = (1 << 6),
} hal_capability_t;

// Forward declarations
typedef struct hal_pin hal_pin_t;
typedef struct hal_provider hal_provider_t;

// Pin operations interface
typedef struct {
    // Digital I/O operations
    void (*digital_set_direction)(hal_pin_t *pin, bool output);
    void (*digital_set_value)(hal_pin_t *pin, bool value);
    bool (*digital_get_value)(hal_pin_t *pin);
    void (*digital_set_pull)(hal_pin_t *pin, int pull_mode);
    
    // Analog operations
    uint16_t (*analog_read)(hal_pin_t *pin);
    void (*analog_write)(hal_pin_t *pin, uint16_t value);
    
    // Cleanup
    void (*pin_deinit)(hal_pin_t *pin);
} hal_pin_ops_t;

// I2C operations interface
typedef struct {
    mp_obj_t (*i2c_create)(mp_obj_t scl_pin, mp_obj_t sda_pin, uint32_t frequency);
    bool (*i2c_try_lock)(mp_obj_t i2c_obj);
    void (*i2c_unlock)(mp_obj_t i2c_obj);
    void (*i2c_scan)(mp_obj_t i2c_obj, uint8_t *addresses, size_t *count);
    void (*i2c_writeto)(mp_obj_t i2c_obj, uint8_t addr, const uint8_t *data, size_t len);
    void (*i2c_readfrom)(mp_obj_t i2c_obj, uint8_t addr, uint8_t *data, size_t len);
    void (*i2c_deinit)(mp_obj_t i2c_obj);
} hal_i2c_ops_t;

// SPI operations interface
typedef struct {
    mp_obj_t (*spi_create)(mp_obj_t clk_pin, mp_obj_t mosi_pin, mp_obj_t miso_pin);
    void (*spi_configure)(mp_obj_t spi_obj, uint32_t baudrate, uint8_t polarity, uint8_t phase);
    bool (*spi_try_lock)(mp_obj_t spi_obj);
    void (*spi_unlock)(mp_obj_t spi_obj);
    void (*spi_write)(mp_obj_t spi_obj, const uint8_t *data, size_t len);
    void (*spi_readinto)(mp_obj_t spi_obj, uint8_t *buffer, size_t len);
    void (*spi_deinit)(mp_obj_t spi_obj);
} hal_spi_ops_t;

// Hardware provider structure
struct hal_provider {
    const char *name;                    // Provider name (e.g., "javascript", "simulation")
    hal_capability_t capabilities;       // Supported hardware capabilities
    
    // Operation interfaces
    const hal_pin_ops_t *pin_ops;       // Pin operations
    const hal_i2c_ops_t *i2c_ops;       // I2C operations  
    const hal_spi_ops_t *spi_ops;       // SPI operations
    
    // Provider lifecycle
    bool (*init)(void);                  // Initialize provider
    void (*deinit)(void);               // Cleanup provider
    
    // Board configuration
    mp_obj_t (*get_board_module)(void); // Return board pin definitions
};

// Provider registration and management
bool hal_register_provider(const hal_provider_t *provider);
const hal_provider_t *hal_get_provider(void);
const hal_provider_t *hal_get_provider_by_name(const char *name);
bool hal_has_capability(hal_capability_t capability);

// Provider initialization
void hal_provider_init(void);
void hal_provider_deinit(void);

#endif // HAL_PROVIDER_H
```

#### hal_pin.h - Pin Abstraction
```c
#ifndef HAL_PIN_H
#define HAL_PIN_H

#include "py/obj.h"
#include "hal_provider.h"

// Pin pull modes
typedef enum {
    HAL_PULL_NONE = 0,
    HAL_PULL_UP = 1,
    HAL_PULL_DOWN = 2,
} hal_pull_t;

// Pin structure
struct hal_pin {
    mp_obj_base_t base;
    uint16_t number;                     // Pin number
    const char *name;                    // Pin name (e.g., "GP25", "D0")
    hal_capability_t capabilities;       // What this pin supports
    void *provider_data;                 // Provider-specific data
    const hal_provider_t *provider;      // Hardware provider
};

// Pin creation and management
hal_pin_t *hal_pin_create(uint16_t number, const char *name, hal_capability_t caps);
hal_pin_t *hal_pin_find_by_name(const char *name);
hal_pin_t *hal_pin_find_by_number(uint16_t number);

// Pin capability checking
bool hal_pin_supports_digital(const hal_pin_t *pin);
bool hal_pin_supports_analog_in(const hal_pin_t *pin);
bool hal_pin_supports_analog_out(const hal_pin_t *pin);

// MicroPython object interface
extern const mp_obj_type_t hal_pin_type;
mp_obj_t hal_pin_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args);

#endif // HAL_PIN_H
```

### HAL Module Implementations

#### hal_digitalio.c - Digital I/O HAL
```c
#include "hal_provider.h"
#include "hal_pin.h"
#include "shared-bindings/digitalio/DigitalInOut.h"

// HAL-based DigitalInOut implementation
typedef struct {
    mp_obj_base_t base;
    hal_pin_t *pin;
    bool output_mode;
    hal_pull_t pull_mode;
} hal_digitalio_digitalinout_obj_t;

// Create DigitalInOut using HAL
mp_obj_t hal_digitalio_digitalinout_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // Get pin argument
    hal_pin_t *pin = (hal_pin_t *)args[0];
    
    // Verify pin supports digital I/O
    if (!hal_pin_supports_digital(pin)) {
        mp_raise_ValueError("Pin does not support digital I/O");
    }
    
    // Create DigitalInOut object
    hal_digitalio_digitalinout_obj_t *self = m_new_obj(hal_digitalio_digitalinout_obj_t);
    self->base.type = &digitalio_digitalinout_type;
    self->pin = pin;
    self->output_mode = false;  // Default to input
    self->pull_mode = HAL_PULL_NONE;
    
    return MP_OBJ_FROM_PTR(self);
}

// Set pin direction via HAL
void hal_digitalio_digitalinout_set_direction(mp_obj_t self_in, digitalio_direction_t direction) {
    hal_digitalio_digitalinout_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const hal_provider_t *provider = hal_get_provider();
    
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_direction) {
        bool output = (direction == DIRECTION_OUTPUT);
        provider->pin_ops->digital_set_direction(self->pin, output);
        self->output_mode = output;
    }
}

// Set pin value via HAL
void hal_digitalio_digitalinout_set_value(mp_obj_t self_in, bool value) {
    hal_digitalio_digitalinout_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const hal_provider_t *provider = hal_get_provider();
    
    if (!self->output_mode) {
        mp_raise_ValueError("Pin not configured as output");
    }
    
    if (provider && provider->pin_ops && provider->pin_ops->digital_set_value) {
        provider->pin_ops->digital_set_value(self->pin, value);
    }
}

// Get pin value via HAL
bool hal_digitalio_digitalinout_get_value(mp_obj_t self_in) {
    hal_digitalio_digitalinout_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const hal_provider_t *provider = hal_get_provider();
    
    if (provider && provider->pin_ops && provider->pin_ops->digital_get_value) {
        return provider->pin_ops->digital_get_value(self->pin);
    }
    
    return false;  // Default fallback
}
```

### HAL Build Configuration

#### variants/hal/mpconfigvariant.mk
```makefile
# CircuitPython WebAssembly HAL Variant
# Layer 2: Hardware Abstraction Layer

# Include Layer 1 (minimal interpreter)
include $(VARIANT_DIR)/../minimal-interpreter/mpconfigvariant.mk

# Enable hardware modules with HAL implementation
CIRCUITPY_BOARD = 1
CIRCUITPY_DIGITALIO = 1
CIRCUITPY_ANALOGIO = 1
CIRCUITPY_BUSIO = 1
CIRCUITPY_MICROCONTROLLER = 1

# HAL-specific configuration
CIRCUITPY_HAL_PROVIDER = 1

# HAL source files
SRC_HAL = \
    hal_provider.c \
    hal_pin.c \
    hal_digitalio.c \
    hal_analogio.c \
    hal_busio.c \
    hal_board.c

SRC_C += $(addprefix $(VARIANT_DIR)/, $(SRC_HAL))

# No specific JavaScript files (environment-agnostic)
SRC_JS =

# HAL exports (provider registration functions)
LDFLAGS += -s EXPORTED_FUNCTIONS="_mp_js_init,_mp_js_init_with_heap,_mp_js_do_str,_mp_js_process_char,_hal_register_js_provider"

# Size optimization
CFLAGS += -DHAL_MINIMAL_FOOTPRINT=1
```

#### variants/hal/mpconfigvariant.h
```c
// Include minimal interpreter base
#include "../minimal-interpreter/mpconfigvariant.h"

// Enable hardware modules with HAL backend
#undef CIRCUITPY_BOARD
#define CIRCUITPY_BOARD (1)

#undef CIRCUITPY_DIGITALIO  
#define CIRCUITPY_DIGITALIO (1)

#undef CIRCUITPY_ANALOGIO
#define CIRCUITPY_ANALOGIO (1)

#undef CIRCUITPY_BUSIO
#define CIRCUITPY_BUSIO (1)

#undef CIRCUITPY_MICROCONTROLLER
#define CIRCUITPY_MICROCONTROLLER (1)

// HAL-specific features
#define CIRCUITPY_HAL_PROVIDER (1)
#define HAL_MAX_PINS (64)
#define HAL_MAX_PROVIDERS (4)

// Default board configuration for HAL
#define MICROPY_HW_BOARD_NAME "WebAssembly-HAL"
#define MICROPY_HW_MCU_NAME "HAL-Provider"

// Enable provider discovery
#define HAL_PROVIDER_DISCOVERY (1)
```

## Provider Implementation Examples

### Stub Provider (Default Fallback)
```c
// hal_stub_provider.c - No-op implementation for testing

static void stub_digital_set_direction(hal_pin_t *pin, bool output) {
    // No-op: stub implementation
}

static void stub_digital_set_value(hal_pin_t *pin, bool value) {
    // No-op: stub implementation  
}

static bool stub_digital_get_value(hal_pin_t *pin) {
    return false;  // Always return false
}

static const hal_pin_ops_t stub_pin_ops = {
    .digital_set_direction = stub_digital_set_direction,
    .digital_set_value = stub_digital_set_value,
    .digital_get_value = stub_digital_get_value,
    .digital_set_pull = NULL,
    .analog_read = NULL,
    .analog_write = NULL,
    .pin_deinit = NULL,
};

static bool stub_provider_init(void) {
    return true;  // Always succeeds
}

static void stub_provider_deinit(void) {
    // No cleanup needed
}

static mp_obj_t stub_get_board_module(void) {
    // Return minimal board with GP0-GP31 pins
    // Implementation would create board module
    return mp_const_none;
}

const hal_provider_t hal_stub_provider = {
    .name = "stub",
    .capabilities = HAL_CAP_DIGITAL_IO,  // Only digital I/O
    .pin_ops = &stub_pin_ops,
    .i2c_ops = NULL,
    .spi_ops = NULL,
    .init = stub_provider_init,
    .deinit = stub_provider_deinit,
    .get_board_module = stub_get_board_module,
};
```

### JavaScript Provider Interface (For Environment Integration)
```c
// hal_js_provider.c - JavaScript provider for browser/Node.js

// JavaScript bridge functions (implemented in environment)
extern void js_digital_set_direction(uint16_t pin, bool output);
extern void js_digital_set_value(uint16_t pin, bool value);
extern bool js_digital_get_value(uint16_t pin);
extern uint16_t js_analog_read(uint16_t pin);
extern void js_analog_write(uint16_t pin, uint16_t value);

static void js_provider_digital_set_direction(hal_pin_t *pin, bool output) {
    js_digital_set_direction(pin->number, output);
}

static void js_provider_digital_set_value(hal_pin_t *pin, bool value) {
    js_digital_set_value(pin->number, value);
}

static bool js_provider_digital_get_value(hal_pin_t *pin) {
    return js_digital_get_value(pin->number);
}

static uint16_t js_provider_analog_read(hal_pin_t *pin) {
    return js_analog_read(pin->number);
}

static const hal_pin_ops_t js_pin_ops = {
    .digital_set_direction = js_provider_digital_set_direction,
    .digital_set_value = js_provider_digital_set_value,
    .digital_get_value = js_provider_digital_get_value,
    .digital_set_pull = NULL,
    .analog_read = js_provider_analog_read,
    .analog_write = js_provider_analog_write,
    .pin_deinit = NULL,
};

const hal_provider_t hal_js_provider = {
    .name = "javascript",
    .capabilities = HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT,
    .pin_ops = &js_pin_ops,
    .i2c_ops = NULL,  // Could be implemented
    .spi_ops = NULL,  // Could be implemented
    .init = NULL,
    .deinit = NULL,
    .get_board_module = NULL,  // Environment provides board
};

// Export for JavaScript registration
EMSCRIPTEN_KEEPALIVE void hal_register_js_provider(void) {
    hal_register_provider(&hal_js_provider);
}
```

## Integration Workflow

### Phase 1: HAL Variant Creation
```bash
# Create HAL variant
make VARIANT=hal

# Expected output: ~400-500KB WASM
# Includes hardware modules with provider interfaces
```

### Phase 2: Provider Development
```javascript
// Environment-specific provider (browser example)
class BrowserHardwareProvider {
    constructor() {
        this.pinStates = new Map();
    }
    
    digitalSetDirection(pin, output) {
        console.log(`Pin ${pin} direction: ${output ? 'output' : 'input'}`);
    }
    
    digitalSetValue(pin, value) {
        this.pinStates.set(pin, value);
        console.log(`Pin ${pin} = ${value}`);
    }
    
    digitalGetValue(pin) {
        return this.pinStates.get(pin) || false;
    }
}

// Register provider with HAL
const provider = new BrowserHardwareProvider();
Module._hal_register_js_provider(provider);
```

### Phase 3: Usage Testing
```python
# HAL-based CircuitPython code
import board
import digitalio

# This works with ANY registered provider
led = digitalio.DigitalInOut(board.GP25)
led.direction = digitalio.Direction.OUTPUT
led.value = True
```

## Benefits of HAL Architecture

### For Development
- **Clean separation**: Hardware API independent of implementation
- **Testing**: Stub providers for unit testing
- **Simulation**: Multiple provider backends
- **Portability**: Same code works across environments

### For Deployment
- **Size optimization**: Include only needed capabilities
- **Runtime flexibility**: Provider selection at initialization
- **Environment adaptation**: Custom providers for specific platforms
- **Graceful degradation**: Fallback behaviors when hardware unavailable

### For Maintenance
- **Interface stability**: Hardware API doesn't change with provider updates
- **Provider isolation**: Implementation bugs don't affect HAL
- **Incremental development**: Add capabilities without breaking changes
- **Clear boundaries**: Well-defined responsibilities

## Migration Path

1. **Layer 1 → HAL**: Add hardware modules with provider interfaces
2. **HAL → Environment**: Register provider for specific runtime
3. **Testing**: Use stub provider for validation
4. **Production**: JavaScript/Native provider for actual hardware

This HAL architecture provides the foundation for supporting multiple hardware backends while maintaining clean separation between the Python interpreter, hardware abstraction, and environment-specific implementations.

## Refactored bin/standard Implementation Plan

### Current Architecture Analysis

**bin/core** (Layer 1 - Pure Interpreter):
- Target: ~150KB WASM 
- Features: Core Python interpreter, no hardware modules
- Memory: 8MB initial, 256KB stack
- Use case: Code validation, syntax checking

**bin/standard** (Current):
- Target: ~400-600KB WASM
- Features: Full JavaScript FFI, hardware modules with JavaScript backends
- Memory: 16MB initial, 512KB stack
- JavaScript integration: Proxy system, direct JS calls

### Proposed Refactored bin/standard (Layer 2 - HAL)

Transform bin/standard to use HAL Layer 2 architecture while maintaining size efficiency and JavaScript integration.

#### Design Goals

1. **Build on bin/core**: Extend core interpreter with HAL provider system
2. **Maintain JavaScript integration**: HAL providers can use JavaScript backends
3. **Size target**: 300-400KB WASM (between current core and standard)
4. **Runtime flexibility**: Support multiple provider backends
5. **Backward compatibility**: Existing JavaScript code continues working

#### Implementation Strategy

##### Phase 1: HAL Core Integration

**File Structure:**
```
bin/standard/
├── mpconfigvariant.mk          # HAL-based standard build  
├── mpconfigvariant.h           # HAL feature configuration
├── hal_provider.c              # Provider registration system
├── hal_pin.c                   # Pin abstraction implementation  
├── hal_digitalio.c             # HAL-based digitalio module
├── hal_analogio.c              # HAL-based analogio module
├── hal_busio.c                 # HAL-based busio module
├── hal_board.c                 # HAL-based board module
├── js_provider.c               # JavaScript hardware provider
├── api.js                      # Enhanced with provider support
└── manifest.py                 # Frozen modules
```

##### Phase 2: Build Configuration

**bin/standard/mpconfigvariant.mk:**
```makefile
# HAL-Extended Standard Build
# Layer 2: Core interpreter + Hardware Abstraction Layer

# Include bin/core as foundation  
include $(VARIANT_DIR)/../core/mpconfigvariant.mk

# Override target name
PROG = circuitpython-hal-standard.mjs

# Re-enable hardware modules with HAL backend
CIRCUITPY_BOARD = 1
CIRCUITPY_DIGITALIO = 1
CIRCUITPY_ANALOGIO = 1  
CIRCUITPY_BUSIO = 1
CIRCUITPY_MICROCONTROLLER = 1

# Enable HAL provider system
CIRCUITPY_HAL_PROVIDER = 1

# HAL implementation sources
SRC_HAL = \
    hal_provider.c \
    hal_pin.c \
    hal_digitalio.c \
    hal_analogio.c \
    hal_busio.c \
    hal_board.c \
    js_provider.c

SRC_C += $(addprefix $(VARIANT_DIR)/, $(SRC_HAL))

# JavaScript integration (essential for provider backends)
SRC_JS = \
    api.js \
    objpyproxy.js \
    proxy_js.js

# Restore JavaScript FFI for provider communication
MICROPY_PY_JSFFI = 1

# HAL + JavaScript provider exports
LDFLAGS += -s EXPORTED_FUNCTIONS="_free,_malloc,_mp_js_init,_mp_js_init_with_heap,_mp_js_post_init,_mp_js_repl_init,_mp_js_repl_process_char,_mp_hal_get_interrupt_char,_mp_js_do_exec,_mp_js_do_exec_async,_hal_register_provider,_hal_register_js_provider,_proxy_c_init,_proxy_c_to_js_call"

# Maintain core's memory efficiency
LDFLAGS += -s INITIAL_MEMORY=12MB -s STACK_SIZE=384KB

# Provider discovery and registration
LDFLAGS += --js-library library.js
```

**bin/standard/mpconfigvariant.h:**
```c
// Include core interpreter as foundation
#include "../core/mpconfigvariant.h"

// Override: Enable hardware modules with HAL backend
#undef CIRCUITPY_BOARD
#define CIRCUITPY_BOARD (1)

#undef CIRCUITPY_DIGITALIO  
#define CIRCUITPY_DIGITALIO (1)

#undef CIRCUITPY_ANALOGIO
#define CIRCUITPY_ANALOGIO (1)

#undef CIRCUITPY_BUSIO
#define CIRCUITPY_BUSIO (1)

#undef CIRCUITPY_MICROCONTROLLER
#define CIRCUITPY_MICROCONTROLLER (1)

// Enable HAL provider system
#define CIRCUITPY_HAL_PROVIDER (1)
#define HAL_MAX_PINS (32)
#define HAL_MAX_PROVIDERS (3)

// Override: Re-enable JavaScript FFI for providers
#undef MICROPY_PY_JSFFI
#define MICROPY_PY_JSFFI (1)

// Override core identification
#undef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME "HAL-Standard"

#undef MICROPY_HW_MCU_NAME  
#define MICROPY_HW_MCU_NAME "WebAssembly-HAL"

// Provider integration flags
#define HAL_JAVASCRIPT_PROVIDER (1)
#define HAL_STUB_PROVIDER (1)
```

##### Phase 3: JavaScript Provider Implementation

**js_provider.c:**
```c
// JavaScript Hardware Provider for HAL
#include "hal_provider.h"
#include "py/runtime.h"

// JavaScript bridge functions
EM_JS(void, js_digital_set_direction, (int pin, bool output), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetDirection) {
        Module.hardwareProvider.digitalSetDirection(pin, output);
    }
});

EM_JS(void, js_digital_set_value, (int pin, bool value), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetValue) {
        Module.hardwareProvider.digitalSetValue(pin, value);
    }  
});

EM_JS(bool, js_digital_get_value, (int pin), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalGetValue) {
        return Module.hardwareProvider.digitalGetValue(pin);
    }
    return false;
});

// HAL JavaScript provider implementation
static void js_provider_digital_set_direction(hal_pin_t *pin, bool output) {
    js_digital_set_direction(pin->number, output);
}

static void js_provider_digital_set_value(hal_pin_t *pin, bool value) {
    js_digital_set_value(pin->number, value);
}

static bool js_provider_digital_get_value(hal_pin_t *pin) {
    return js_digital_get_value(pin->number);
}

static const hal_pin_ops_t js_pin_ops = {
    .digital_set_direction = js_provider_digital_set_direction,
    .digital_set_value = js_provider_digital_set_value,
    .digital_get_value = js_provider_digital_get_value,
    // Additional ops...
};

const hal_provider_t hal_javascript_provider = {
    .name = "javascript", 
    .capabilities = HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT,
    .pin_ops = &js_pin_ops,
    .init = NULL,
    .deinit = NULL,
    .get_board_module = NULL,
};

// Auto-register JavaScript provider
EMSCRIPTEN_KEEPALIVE void hal_register_js_provider(void) {
    hal_register_provider(&hal_javascript_provider);
}
```

##### Phase 4: Enhanced JavaScript API

**Enhanced api.js:**
```javascript
// HAL-aware CircuitPython module creation
function createHALStandardCircuitPython(options = {}) {
    const module = _createCircuitPythonModule();
    
    // Register hardware provider if supplied
    if (options.hardwareProvider) {
        module.hardwareProvider = options.hardwareProvider;
        module._hal_register_js_provider();
    }
    
    return module;
}

// Provider interface for JavaScript environments
class DefaultHardwareProvider {
    constructor() {
        this.pinStates = new Map();
    }
    
    digitalSetDirection(pin, output) {
        console.log(`HAL: Pin ${pin} -> ${output ? 'OUTPUT' : 'INPUT'}`);
    }
    
    digitalSetValue(pin, value) {
        this.pinStates.set(pin, value);
        console.log(`HAL: Pin ${pin} = ${value}`);
    }
    
    digitalGetValue(pin) {
        return this.pinStates.get(pin) || false;
    }
    
    analogRead(pin) {
        return Math.floor(Math.random() * 65536); // Simulated
    }
    
    analogWrite(pin, value) {
        console.log(`HAL: Analog pin ${pin} = ${value}`);
    }
}

// Export enhanced API
globalThis.createHALStandardCircuitPython = createHALStandardCircuitPython;
globalThis.DefaultHardwareProvider = DefaultHardwareProvider;
```

#### Migration Benefits

##### Size Optimization
- **Target**: 300-400KB WASM (vs current 400-600KB)
- **Memory**: Efficient 12MB initial (between core's 8MB and standard's 16MB)
- **Modular**: Only include needed provider capabilities

##### Flexibility Gains
- **Multi-backend**: Same binary supports JavaScript, simulation, stub providers
- **Testing**: Built-in stub provider for unit tests
- **Environment adaptation**: Custom providers for Node.js, browser, embedded

##### Developer Experience
- **Familiar API**: Existing CircuitPython hardware code unchanged
- **Provider choice**: Runtime selection of hardware backend
- **Debugging**: Clear separation between Python logic and hardware implementation

##### Backward Compatibility
- **Existing code**: Current JavaScript integration continues working
- **Provider migration**: Gradual transition from direct JS calls to HAL providers
- **API stability**: Hardware modules maintain existing interface

#### Implementation Timeline

**Week 1-2**: HAL provider system integration
- Port hal_provider.c and hal_pin.c from variants/hal 
- Integrate with bin/core build system
- Basic provider registration working

**Week 3-4**: Hardware module conversion  
- Convert digitalio to HAL backend (hal_digitalio.c)
- Convert analogio to HAL backend (hal_analogio.c)
- Test with stub provider

**Week 5-6**: JavaScript provider implementation
- Implement js_provider.c with EM_JS bridges
- Enhanced api.js with provider support  
- Integration testing with browser/Node.js

**Week 7-8**: Testing and optimization
- Comprehensive testing across provider types
- Size optimization and performance tuning
- Documentation updates

This refactored bin/standard provides a clean migration path from the current JavaScript-integrated build to a HAL-based architecture while maintaining compatibility and improving flexibility.