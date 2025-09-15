#include "nodejs_bridge.h"
#include "nodejs_hardware_state.h"
#include <stdio.h>
#include <string.h>
#include <emscripten.h>

// Node.js Bridge Implementation - Abstracted JavaScript Interface
// This replaces direct EM_JS calls with a clean, testable abstraction

static nodejs_bridge_mode_t g_bridge_mode = NODEJS_BRIDGE_MODE_HYBRID;
static bool g_bridge_initialized = false;
static bool g_batching_enabled = true;
static bool g_cli_output_enabled = true;
static uint32_t g_javascript_call_count = 0;
static nodejs_bridge_error_t g_last_error = NODEJS_BRIDGE_OK;

// Low-level JavaScript interface (still using EM_JS but abstracted)
// These are now internal implementation details, not public API

EM_JS(bool, _nodejs_bridge_check_native_hardware, (), {
    // Check if native Node.js hardware APIs are available
    return (typeof global !== 'undefined' &&
            global.nodeHardwareBridge &&
            typeof global.nodeHardwareBridge.isHardwareAvailable === 'function' &&
            global.nodeHardwareBridge.isHardwareAvailable());
});

EM_JS(bool, _nodejs_bridge_digital_write_native, (const char* pin_name, bool value), {
    try {
        const name = UTF8ToString(pin_name);
        if (typeof global !== 'undefined' && global.nodeHardwareBridge) {
            global.nodeHardwareBridge.digitalWrite(name, value);
            return true;
        }
        return false;
    } catch (e) {
        console.error('[Node.js Bridge] Digital write error:', e);
        return false;
    }
});

EM_JS(bool, _nodejs_bridge_digital_read_native, (const char* pin_name, bool* value_out), {
    try {
        const name = UTF8ToString(pin_name);
        if (typeof global !== 'undefined' && global.nodeHardwareBridge) {
            const value = global.nodeHardwareBridge.digitalRead(name);
            setValue(value_out, value ? 1 : 0, 'i32');
            return true;
        }
        return false;
    } catch (e) {
        console.error('[Node.js Bridge] Digital read error:', e);
        return false;
    }
});

EM_JS(bool, _nodejs_bridge_analog_read_native, (const char* pin_name, uint16_t* value_out), {
    try {
        const name = UTF8ToString(pin_name);
        if (typeof global !== 'undefined' && global.nodeHardwareBridge) {
            const value = global.nodeHardwareBridge.analogRead(name);
            setValue(value_out, value || 0, 'i16');
            return true;
        }
        return false;
    } catch (e) {
        console.error('[Node.js Bridge] Analog read error:', e);
        return false;
    }
});

EM_JS(void, _nodejs_bridge_log_operation, (const char* operation, const char* details), {
    if (Module.nodejsBridgeConfig && Module.nodejsBridgeConfig.enableLogging) {
        const op = UTF8ToString(operation);
        const det = UTF8ToString(details);
        console.log(`[Node.js Bridge] ${op}: ${det}`);
    }
});

// Bridge initialization
void nodejs_bridge_init(nodejs_bridge_mode_t mode) {
    if (g_bridge_initialized) {
        return;
    }

    g_bridge_mode = mode;

    // Auto-detect hardware capabilities in hybrid mode
    if (mode == NODEJS_BRIDGE_MODE_HYBRID) {
        if (_nodejs_bridge_check_native_hardware()) {
            g_bridge_mode = NODEJS_BRIDGE_MODE_NATIVE;
            printf("[Node.js Bridge] Native hardware detected, using native mode\n");
        } else {
            g_bridge_mode = NODEJS_BRIDGE_MODE_SIMULATION;
            printf("[Node.js Bridge] No native hardware, using simulation mode\n");
        }
    }

    // Initialize hardware state system
    nodejs_hardware_state_init();

    g_bridge_initialized = true;
    g_last_error = NODEJS_BRIDGE_OK;

    printf("[Node.js Bridge] Initialized in %s mode\n",
           g_bridge_mode == NODEJS_BRIDGE_MODE_NATIVE ? "native" :
           g_bridge_mode == NODEJS_BRIDGE_MODE_SIMULATION ? "simulation" : "hybrid");
}

void nodejs_bridge_deinit(void) {
    if (!g_bridge_initialized) {
        return;
    }

    nodejs_hardware_state_deinit();

    printf("[Node.js Bridge] Deinitialized (%u JavaScript calls made)\n",
           g_javascript_call_count);

    g_bridge_initialized = false;
    g_javascript_call_count = 0;
}

nodejs_bridge_mode_t nodejs_bridge_get_mode(void) {
    return g_bridge_mode;
}

// Digital I/O bridge operations
bool nodejs_bridge_digital_write(const char* pin_name, bool value) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    // Always update C-side state for consistency
    nodejs_pin_set_digital(pin_name, value);

    bool success = false;
    switch (g_bridge_mode) {
        case NODEJS_BRIDGE_MODE_NATIVE:
            success = _nodejs_bridge_digital_write_native(pin_name, value);
            if (success) {
                g_javascript_call_count++;
            }
            break;

        case NODEJS_BRIDGE_MODE_SIMULATION:
            success = true; // Simulation always succeeds
            if (g_cli_output_enabled) {
                printf("[Node.js HAL] Pin %s = %s\n", pin_name, value ? "HIGH" : "LOW");
            }
            break;

        case NODEJS_BRIDGE_MODE_HYBRID:
            // Shouldn't reach here after init, but handle gracefully
            success = true;
            break;
    }

    if (!success) {
        g_last_error = NODEJS_BRIDGE_ERROR_OPERATION_FAILED;
    }

    return success;
}

bool nodejs_bridge_digital_read(const char* pin_name, bool* value_out) {
    if (!g_bridge_initialized || value_out == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    bool success = false;
    bool value = false;

    switch (g_bridge_mode) {
        case NODEJS_BRIDGE_MODE_NATIVE:
            success = _nodejs_bridge_digital_read_native(pin_name, &value);
            if (success) {
                g_javascript_call_count++;
                // Update C-side state with native value
                nodejs_pin_set_digital(pin_name, value);
            }
            break;

        case NODEJS_BRIDGE_MODE_SIMULATION:
            // Get value from C-side state
            value = nodejs_pin_get_digital(pin_name);
            success = true;

            if (g_cli_output_enabled) {
                printf("[Node.js HAL] Pin %s read = %s\n", pin_name, value ? "HIGH" : "LOW");
            }
            break;

        case NODEJS_BRIDGE_MODE_HYBRID:
            value = nodejs_pin_get_digital(pin_name);
            success = true;
            break;
    }

    if (success) {
        *value_out = value;
    } else {
        g_last_error = NODEJS_BRIDGE_ERROR_OPERATION_FAILED;
    }

    return success;
}

bool nodejs_bridge_digital_set_direction(const char* pin_name, bool output) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_pin_set_mode(pin_name, output ? NODEJS_PIN_MODE_OUTPUT : NODEJS_PIN_MODE_INPUT);

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] Pin %s direction: %s\n",
               pin_name, output ? "OUTPUT" : "INPUT");
    }

    return true; // Direction setting always succeeds in our implementation
}

bool nodejs_bridge_digital_set_pull(const char* pin_name, int pull_mode) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_pull_mode_t pull = (pull_mode == 1) ? NODEJS_PULL_UP :
                              (pull_mode == 2) ? NODEJS_PULL_DOWN : NODEJS_PULL_NONE;

    nodejs_pin_set_pull(pin_name, pull);

    if (g_cli_output_enabled) {
        const char* pull_str = (pull == NODEJS_PULL_UP) ? "UP" :
                               (pull == NODEJS_PULL_DOWN) ? "DOWN" : "NONE";
        printf("[Node.js HAL] Pin %s pull: %s\n", pin_name, pull_str);
    }

    return true;
}

// Analog I/O bridge operations
bool nodejs_bridge_analog_write(const char* pin_name, uint16_t value) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_pin_set_analog(pin_name, value);

    if (g_cli_output_enabled) {
        float voltage = (value / 65535.0f) * 3.3f; // Assume 3.3V reference
        printf("[Node.js HAL] Pin %s analog = %u (%.2fV)\n", pin_name, value, (double)voltage);
    }

    return true;
}

bool nodejs_bridge_analog_read(const char* pin_name, uint16_t* value_out) {
    if (!g_bridge_initialized || value_out == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    uint16_t value = 0;
    bool success = false;

    switch (g_bridge_mode) {
        case NODEJS_BRIDGE_MODE_NATIVE:
            success = _nodejs_bridge_analog_read_native(pin_name, &value);
            if (success) {
                g_javascript_call_count++;
                nodejs_pin_set_analog(pin_name, value);
            }
            break;

        case NODEJS_BRIDGE_MODE_SIMULATION:
            value = nodejs_pin_get_analog(pin_name);
            success = true;
            break;

        case NODEJS_BRIDGE_MODE_HYBRID:
            value = nodejs_pin_get_analog(pin_name);
            success = true;
            break;
    }

    if (success) {
        *value_out = value;

        if (g_cli_output_enabled) {
            float voltage = (value / 65535.0f) * 3.3f;
            printf("[Node.js HAL] Pin %s analog read = %u (%.2fV)\n", pin_name, value, (double)voltage);
        }
    } else {
        g_last_error = NODEJS_BRIDGE_ERROR_OPERATION_FAILED;
    }

    return success;
}

// I2C bridge operations
int nodejs_bridge_i2c_create(const char* scl_pin, const char* sda_pin, uint32_t frequency) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return -1;
    }

    // For now, use pin numbers 0,1 as placeholder - real implementation would parse pin names
    int bus_id = nodejs_i2c_create_bus(0, 1, frequency);

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] I2C bus %d created: SCL=%s, SDA=%s, %uHz\n",
               bus_id, scl_pin, sda_pin, frequency);
    }

    return bus_id;
}

bool nodejs_bridge_i2c_scan(int bus_id, uint8_t* addresses, uint8_t* count) {
    if (!g_bridge_initialized || addresses == NULL || count == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_i2c_state_t* bus = nodejs_i2c_get_bus(bus_id);
    if (bus == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_INVALID_PIN;
        return false;
    }

    *count = bus->device_count;
    for (uint8_t i = 0; i < bus->device_count; i++) {
        addresses[i] = bus->device_addresses[i];
    }

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] I2C scan found %u devices on bus %d\n", *count, bus_id);
    }

    return true;
}

bool nodejs_bridge_i2c_write(int bus_id, uint8_t addr, const uint8_t* data, size_t len) {
    if (!g_bridge_initialized || data == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_i2c_state_t* bus = nodejs_i2c_get_bus(bus_id);
    if (bus == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_INVALID_PIN;
        return false;
    }

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] I2C write to 0x%02X: %zu bytes on bus %d\n", addr, len, bus_id);
    }

    // In simulation mode, just log the operation
    return true;
}

bool nodejs_bridge_i2c_read(int bus_id, uint8_t addr, uint8_t* data, size_t len) {
    if (!g_bridge_initialized || data == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_i2c_state_t* bus = nodejs_i2c_get_bus(bus_id);
    if (bus == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_INVALID_PIN;
        return false;
    }

    // In simulation mode, fill with zeros
    memset(data, 0, len);

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] I2C read from 0x%02X: %zu bytes on bus %d\n", addr, len, bus_id);
    }

    return true;
}

// SPI bridge operations
int nodejs_bridge_spi_create(const char* clk_pin, const char* mosi_pin, const char* miso_pin) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return -1;
    }

    // For now, use pin numbers 0,1,2 as placeholder
    int bus_id = nodejs_spi_create_bus(0, 1, 2);

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] SPI bus %d created: CLK=%s, MOSI=%s, MISO=%s\n",
               bus_id, clk_pin, mosi_pin, miso_pin);
    }

    return bus_id;
}

bool nodejs_bridge_spi_configure(int bus_id, uint32_t baudrate, uint8_t polarity, uint8_t phase) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_spi_state_t* bus = nodejs_spi_get_bus(bus_id);
    if (bus == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_INVALID_PIN;
        return false;
    }

    nodejs_spi_configure(bus_id, baudrate, polarity, phase);

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] SPI bus %d configured: %uHz, pol=%u, phase=%u\n",
               bus_id, baudrate, polarity, phase);
    }

    return true;
}

bool nodejs_bridge_spi_transfer(int bus_id, const uint8_t* tx_data, uint8_t* rx_data, size_t len) {
    if (!g_bridge_initialized) {
        g_last_error = NODEJS_BRIDGE_ERROR_NOT_INITIALIZED;
        return false;
    }

    nodejs_spi_state_t* bus = nodejs_spi_get_bus(bus_id);
    if (bus == NULL) {
        g_last_error = NODEJS_BRIDGE_ERROR_INVALID_PIN;
        return false;
    }

    // In simulation mode, copy tx to rx or fill with zeros
    if (rx_data) {
        if (tx_data) {
            memcpy(rx_data, tx_data, len);
        } else {
            memset(rx_data, 0, len);
        }
    }

    if (g_cli_output_enabled) {
        printf("[Node.js HAL] SPI transfer: %zu bytes on bus %d\n", len, bus_id);
    }

    return true;
}

// Performance optimization functions
void nodejs_bridge_enable_batching(bool enable) {
    g_batching_enabled = enable;
    if (g_cli_output_enabled) {
        printf("[Node.js Bridge] Batching %s\n", enable ? "enabled" : "disabled");
    }
}

void nodejs_bridge_flush_operations(void) {
    if (g_batching_enabled) {
        nodejs_hardware_sync_to_javascript();
    }
}

uint32_t nodejs_bridge_get_call_count(void) {
    return g_javascript_call_count;
}

// CLI-specific features
void nodejs_bridge_enable_cli_output(bool enable) {
    g_cli_output_enabled = enable;
}

// Hardware capability detection
bool nodejs_bridge_has_native_hardware(void) {
    return (g_bridge_mode == NODEJS_BRIDGE_MODE_NATIVE);
}

// Error handling
nodejs_bridge_error_t nodejs_bridge_get_last_error(void) {
    return g_last_error;
}

const char* nodejs_bridge_error_string(nodejs_bridge_error_t error) {
    switch (error) {
        case NODEJS_BRIDGE_OK:
            return "No error";
        case NODEJS_BRIDGE_ERROR_NOT_INITIALIZED:
            return "Bridge not initialized";
        case NODEJS_BRIDGE_ERROR_HARDWARE_UNAVAILABLE:
            return "Hardware unavailable";
        case NODEJS_BRIDGE_ERROR_INVALID_PIN:
            return "Invalid pin";
        case NODEJS_BRIDGE_ERROR_OPERATION_FAILED:
            return "Operation failed";
        case NODEJS_BRIDGE_ERROR_TIMEOUT:
            return "Operation timeout";
        default:
            return "Unknown error";
    }
}