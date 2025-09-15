#ifndef NODEJS_BRIDGE_H
#define NODEJS_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Abstracted Node.js bridge interface
// This replaces direct EM_JS calls with a clean abstraction layer
// that can be optimized for different deployment scenarios

typedef enum {
    NODEJS_BRIDGE_MODE_NATIVE,      // Direct Node.js hardware access
    NODEJS_BRIDGE_MODE_SIMULATION,  // CLI simulation mode
    NODEJS_BRIDGE_MODE_HYBRID,      // Best of both (auto-detect)
} nodejs_bridge_mode_t;

// Bridge initialization and configuration
void nodejs_bridge_init(nodejs_bridge_mode_t mode);
void nodejs_bridge_deinit(void);
nodejs_bridge_mode_t nodejs_bridge_get_mode(void);

// High-level bridge operations (abstracted from EM_JS)
// These functions handle the complexity of choosing between
// native hardware access vs simulation internally

// Digital I/O bridge operations
bool nodejs_bridge_digital_write(const char* pin_name, bool value);
bool nodejs_bridge_digital_read(const char* pin_name, bool* value_out);
bool nodejs_bridge_digital_set_direction(const char* pin_name, bool output);
bool nodejs_bridge_digital_set_pull(const char* pin_name, int pull_mode);

// Analog I/O bridge operations
bool nodejs_bridge_analog_write(const char* pin_name, uint16_t value);
bool nodejs_bridge_analog_read(const char* pin_name, uint16_t* value_out);

// I2C bridge operations
int nodejs_bridge_i2c_create(const char* scl_pin, const char* sda_pin, uint32_t frequency);
bool nodejs_bridge_i2c_scan(int bus_id, uint8_t* addresses, uint8_t* count);
bool nodejs_bridge_i2c_write(int bus_id, uint8_t addr, const uint8_t* data, size_t len);
bool nodejs_bridge_i2c_read(int bus_id, uint8_t addr, uint8_t* data, size_t len);

// SPI bridge operations
int nodejs_bridge_spi_create(const char* clk_pin, const char* mosi_pin, const char* miso_pin);
bool nodejs_bridge_spi_configure(int bus_id, uint32_t baudrate, uint8_t polarity, uint8_t phase);
bool nodejs_bridge_spi_transfer(int bus_id, const uint8_t* tx_data, uint8_t* rx_data, size_t len);

// Performance optimization functions
void nodejs_bridge_enable_batching(bool enable);        // Batch operations for performance
void nodejs_bridge_flush_operations(void);              // Force flush batched operations
uint32_t nodejs_bridge_get_call_count(void);           // Get JavaScript call statistics

// CLI-specific features
void nodejs_bridge_enable_cli_output(bool enable);      // Enable CLI debug output
void nodejs_bridge_load_config(const char* config_file); // Load Node.js bridge configuration
void nodejs_bridge_save_session(const char* session_file); // Save session for debugging

// Hardware capability detection
bool nodejs_bridge_has_native_hardware(void);          // Detect real hardware availability
bool nodejs_bridge_supports_serial(void);              // Check serialport availability
bool nodejs_bridge_supports_i2c(void);                 // Check native I2C support
bool nodejs_bridge_supports_spi(void);                 // Check native SPI support

// Error handling and diagnostics
typedef enum {
    NODEJS_BRIDGE_OK = 0,
    NODEJS_BRIDGE_ERROR_NOT_INITIALIZED,
    NODEJS_BRIDGE_ERROR_HARDWARE_UNAVAILABLE,
    NODEJS_BRIDGE_ERROR_INVALID_PIN,
    NODEJS_BRIDGE_ERROR_OPERATION_FAILED,
    NODEJS_BRIDGE_ERROR_TIMEOUT,
} nodejs_bridge_error_t;

nodejs_bridge_error_t nodejs_bridge_get_last_error(void);
const char* nodejs_bridge_error_string(nodejs_bridge_error_t error);

#endif // NODEJS_BRIDGE_H