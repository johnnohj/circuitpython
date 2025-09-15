/*
 * Hardware Abstraction Shim Layer for WebAssembly-as-U2IF
 * 
 * This layer intercepts CircuitPython hardware calls and forwards them
 * to the JavaScript bridge, which then sends them to the physical device.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/analogio/AnalogIn.h"
#include "shared-bindings/pwmio/PWMOut.h"

#include "emscripten.h"

// Forward declarations of JavaScript bridge functions
EM_JS(void, js_bridge_digital_write, (const char* pin_name, int value), {
    if (typeof window !== 'undefined' && window.wasmU2IFBridge) {
        const pinName = UTF8ToString(pin_name);
        window.wasmU2IFBridge.queueCommand('digital_write', pinName, value);
    }
});

EM_JS(int, js_bridge_digital_read, (const char* pin_name), {
    if (typeof window !== 'undefined' && window.wasmU2IFBridge) {
        const pinName = UTF8ToString(pin_name);
        // This would need to be async in real implementation
        // For now, return cached value or 0
        const state = window.wasmU2IFBridge.virtualState.get('digital_' + pinName);
        return state || 0;
    }
    return 0;
});

EM_JS(void, js_bridge_pwm_write, (const char* pin_name, double duty_cycle), {
    if (typeof window !== 'undefined' && window.wasmU2IFBridge) {
        const pinName = UTF8ToString(pin_name);
        window.wasmU2IFBridge.queueCommand('pwm_write', pinName, duty_cycle);
    }
});

EM_JS(double, js_bridge_analog_read, (const char* pin_name), {
    if (typeof window !== 'undefined' && window.wasmU2IFBridge) {
        const pinName = UTF8ToString(pin_name);
        const state = window.wasmU2IFBridge.virtualState.get('analog_' + pinName);
        return state || 0.0;
    }
    return 0.0;
});

EM_JS(void, js_bridge_setup_pin, (const char* pin_name, const char* direction), {
    if (typeof window !== 'undefined' && window.wasmU2IFBridge) {
        const pinName = UTF8ToString(pin_name);
        const pinDirection = UTF8ToString(direction);
        window.wasmU2IFBridge.queueCommand('digital_setup', pinName, pinDirection);
    }
});

EM_JS(bool, js_bridge_is_available, (), {
    return (typeof window !== 'undefined' && window.wasmU2IFBridge && window.wasmU2IFBridge.isConnected);
});

// Enhanced DigitalInOut implementation with bridge support
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    digitalio_direction_t direction;
    digitalio_pull_t pull;
    bool open_drain;
    bool value;
    char pin_name[16];  // Store pin name for bridge calls
} bridged_digitalio_digitalinout_obj_t;

// Get pin name from pin object
static void get_pin_name(const mcu_pin_obj_t *pin, char *buffer, size_t buffer_size) {
    // This would need to be implemented based on your pin naming scheme
    // For now, use a simple mapping
    snprintf(buffer, buffer_size, "GP%d", pin->number);
}

// Override digitalio_digitalinout_set_value
void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self, bool value) {
    bridged_digitalio_digitalinout_obj_t *bridged_self = (bridged_digitalio_digitalinout_obj_t *)self;
    
    // Update local state
    bridged_self->value = value;
    
    // Forward to bridge if available
    if (js_bridge_is_available()) {
        js_bridge_digital_write(bridged_self->pin_name, value ? 1 : 0);
    }
}

// Override digitalio_digitalinout_get_value
bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    bridged_digitalio_digitalinout_obj_t *bridged_self = (bridged_digitalio_digitalinout_obj_t *)self;
    
    if (bridged_self->direction == DIGITALIO_DIRECTION_INPUT && js_bridge_is_available()) {
        // Read from physical device via bridge
        int physical_value = js_bridge_digital_read(bridged_self->pin_name);
        bridged_self->value = physical_value != 0;
    }
    
    return bridged_self->value;
}

// Override digitalio_digitalinout_set_direction
void common_hal_digitalio_digitalinout_set_direction(digitalio_digitalinout_obj_t *self, digitalio_direction_t direction) {
    bridged_digitalio_digitalinout_obj_t *bridged_self = (bridged_digitalio_digitalinout_obj_t *)self;
    
    bridged_self->direction = direction;
    
    // Set up pin on physical device
    if (js_bridge_is_available()) {
        const char *dir_str = (direction == DIGITALIO_DIRECTION_OUTPUT) ? "output" : "input";
        js_bridge_setup_pin(bridged_self->pin_name, dir_str);
    }
}

// Override digitalio_digitalinout_construct
void common_hal_digitalio_digitalinout_construct(digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {
    bridged_digitalio_digitalinout_obj_t *bridged_self = (bridged_digitalio_digitalinout_obj_t *)self;
    
    bridged_self->pin = pin;
    bridged_self->direction = DIGITALIO_DIRECTION_INPUT;
    bridged_self->pull = DIGITALIO_PULL_NONE;
    bridged_self->open_drain = false;
    bridged_self->value = false;
    
    // Store pin name for bridge calls
    get_pin_name(pin, bridged_self->pin_name, sizeof(bridged_self->pin_name));
}

// Enhanced PWMOut implementation with bridge support
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    uint32_t frequency;
    uint16_t duty_cycle;
    bool variable_frequency;
    char pin_name[16];
} bridged_pwmio_pwmout_obj_t;

// Override pwmio_pwmout_set_duty_cycle
void common_hal_pwmio_pwmout_set_duty_cycle(pwmio_pwmout_obj_t *self, uint16_t duty_cycle) {
    bridged_pwmio_pwmout_obj_t *bridged_self = (bridged_pwmio_pwmout_obj_t *)self;
    
    bridged_self->duty_cycle = duty_cycle;
    
    // Convert to 0.0-1.0 range and forward to bridge
    if (js_bridge_is_available()) {
        double duty_ratio = (double)duty_cycle / 65535.0;
        js_bridge_pwm_write(bridged_self->pin_name, duty_ratio);
    }
}

// Override pwmio_pwmout_get_duty_cycle
uint16_t common_hal_pwmio_pwmout_get_duty_cycle(pwmio_pwmout_obj_t *self) {
    bridged_pwmio_pwmout_obj_t *bridged_self = (bridged_pwmio_pwmout_obj_t *)self;
    return bridged_self->duty_cycle;
}

// Override pwmio_pwmout_construct
void common_hal_pwmio_pwmout_construct(pwmio_pwmout_obj_t *self, const mcu_pin_obj_t *pin, uint16_t duty, uint32_t frequency, bool variable_frequency) {
    bridged_pwmio_pwmout_obj_t *bridged_self = (bridged_pwmio_pwmout_obj_t *)self;
    
    bridged_self->pin = pin;
    bridged_self->frequency = frequency;
    bridged_self->duty_cycle = duty;
    bridged_self->variable_frequency = variable_frequency;
    
    // Store pin name
    get_pin_name(pin, bridged_self->pin_name, sizeof(bridged_self->pin_name));
    
    // Set up PWM on physical device
    if (js_bridge_is_available()) {
        // Set initial duty cycle
        double duty_ratio = (double)duty / 65535.0;
        js_bridge_pwm_write(bridged_self->pin_name, duty_ratio);
    }
}

// Enhanced AnalogIn implementation with bridge support
typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
    char pin_name[16];
} bridged_analogio_analogin_obj_t;

// Override analogio_analogin_get_value
uint16_t common_hal_analogio_analogin_get_value(analogio_analogin_obj_t *self) {
    bridged_analogio_analogin_obj_t *bridged_self = (bridged_analogio_analogin_obj_t *)self;
    
    if (js_bridge_is_available()) {
        // Read from physical device
        double voltage = js_bridge_analog_read(bridged_self->pin_name);
        
        // Convert voltage (0.0-3.3V) back to 16-bit value
        uint16_t value = (uint16_t)(voltage * 65535.0 / 3.3);
        return value;
    }
    
    return 0;  // Default value if no bridge
}

// Override analogio_analogin_construct  
void common_hal_analogio_analogin_construct(analogio_analogin_obj_t *self, const mcu_pin_obj_t *pin) {
    bridged_analogio_analogin_obj_t *bridged_self = (bridged_analogio_analogin_obj_t *)self;
    
    bridged_self->pin = pin;
    get_pin_name(pin, bridged_self->pin_name, sizeof(bridged_self->pin_name));
}

// Bridge status and control functions
EMSCRIPTEN_KEEPALIVE
bool bridge_is_connected() {
    return js_bridge_is_available();
}

EMSCRIPTEN_KEEPALIVE  
void bridge_set_virtual_digital_value(const char *pin_name, int value) {
    // This allows JavaScript to update virtual pin states
    // that can be read by the WebAssembly code
    // Implementation would store in a global state table
}

EMSCRIPTEN_KEEPALIVE
void bridge_set_virtual_analog_value(const char *pin_name, double voltage) {
    // Similar to digital, but for analog values
}

// Initialize bridge system
EMSCRIPTEN_KEEPALIVE
void bridge_initialize() {
    // Perform any necessary initialization
    // This could be called from JavaScript when bridge is ready
}

// Bridge configuration
typedef struct {
    bool enabled;
    bool bidirectional_sync;
    uint32_t sync_interval_ms;
    char device_type[32];
} bridge_config_t;

static bridge_config_t bridge_config = {
    .enabled = false,
    .bidirectional_sync = true,
    .sync_interval_ms = 100,
    .device_type = "unknown"
};

EMSCRIPTEN_KEEPALIVE
void bridge_configure(bool enabled, bool bidirectional, uint32_t sync_interval) {
    bridge_config.enabled = enabled;
    bridge_config.bidirectional_sync = bidirectional;
    bridge_config.sync_interval_ms = sync_interval;
}

EMSCRIPTEN_KEEPALIVE
bridge_config_t* bridge_get_config() {
    return &bridge_config;
}