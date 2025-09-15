#ifndef NODEJS_HARDWARE_STATE_H
#define NODEJS_HARDWARE_STATE_H

#include <stdint.h>
#include <stdbool.h>

// C-side virtual hardware state management for Node.js HAL
// This replaces JavaScript-side state storage for better performance and control

typedef enum {
    NODEJS_PIN_MODE_INPUT = 0,
    NODEJS_PIN_MODE_OUTPUT = 1,
} nodejs_pin_mode_t;

typedef enum {
    NODEJS_PULL_NONE = 0,
    NODEJS_PULL_UP = 1,
    NODEJS_PULL_DOWN = 2,
} nodejs_pull_mode_t;

typedef struct {
    char name[16];                    // Pin name (e.g., "GP25", "D0")
    bool digital_value;               // Current digital state
    uint16_t analog_value;           // Current analog value (0-65535)
    nodejs_pin_mode_t mode;          // Input/Output mode
    nodejs_pull_mode_t pull;         // Pull resistor setting
    bool in_use;                     // Whether pin is actively being used

    // Performance optimization: cache frequently accessed values
    uint32_t last_access_time;       // For LRU management
    bool state_dirty;                // Needs sync with Node.js side
} nodejs_pin_state_t;

// I2C bus state for CLI automation
typedef struct {
    uint8_t scl_pin;
    uint8_t sda_pin;
    uint32_t frequency;
    bool locked;
    bool in_use;

    // Mock device registry for CLI simulation
    uint8_t device_addresses[8];     // Up to 8 mock devices
    uint8_t device_count;
} nodejs_i2c_state_t;

// SPI bus state for automation
typedef struct {
    uint8_t clk_pin;
    uint8_t mosi_pin;
    uint8_t miso_pin;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    bool locked;
    bool in_use;
} nodejs_spi_state_t;

// Central hardware state manager
typedef struct {
    nodejs_pin_state_t pins[64];     // Support up to 64 pins
    uint8_t pin_count;

    nodejs_i2c_state_t i2c_buses[2]; // Support 2 I2C buses
    uint8_t i2c_count;

    nodejs_spi_state_t spi_buses[2]; // Support 2 SPI buses
    uint8_t spi_count;

    // Performance tracking for CLI automation
    uint32_t total_operations;
    uint32_t javascript_calls;       // Track bridge call overhead
    bool performance_mode;           // Enable optimizations
} nodejs_hardware_state_t;

// Hardware state management functions
void nodejs_hardware_state_init(void);
void nodejs_hardware_state_deinit(void);

// Pin state management - C-side for performance
nodejs_pin_state_t* nodejs_pin_get_state(const char* pin_name);
nodejs_pin_state_t* nodejs_pin_get_or_create_state(const char* pin_name);
void nodejs_pin_set_digital(const char* pin_name, bool value);
bool nodejs_pin_get_digital(const char* pin_name);
void nodejs_pin_set_analog(const char* pin_name, uint16_t value);
uint16_t nodejs_pin_get_analog(const char* pin_name);
void nodejs_pin_set_mode(const char* pin_name, nodejs_pin_mode_t mode);
void nodejs_pin_set_pull(const char* pin_name, nodejs_pull_mode_t pull);

// I2C state management
int nodejs_i2c_create_bus(uint8_t scl_pin, uint8_t sda_pin, uint32_t frequency);
nodejs_i2c_state_t* nodejs_i2c_get_bus(int bus_id);
bool nodejs_i2c_try_lock(int bus_id);
void nodejs_i2c_unlock(int bus_id);
void nodejs_i2c_add_mock_device(int bus_id, uint8_t address);

// SPI state management
int nodejs_spi_create_bus(uint8_t clk_pin, uint8_t mosi_pin, uint8_t miso_pin);
nodejs_spi_state_t* nodejs_spi_get_bus(int bus_id);
bool nodejs_spi_try_lock(int bus_id);
void nodejs_spi_unlock(int bus_id);
void nodejs_spi_configure(int bus_id, uint32_t baudrate, uint8_t polarity, uint8_t phase);

// Performance optimization functions
void nodejs_hardware_enable_performance_mode(bool enable);
void nodejs_hardware_sync_to_javascript(void);    // Batch sync for efficiency
uint32_t nodejs_hardware_get_stats(void);         // Get performance stats

// CLI simulation helpers
void nodejs_hardware_load_simulation_config(const char* config_file);
void nodejs_hardware_save_state_snapshot(const char* filename);
void nodejs_hardware_print_status(void);          // Debug output for CLI

#endif // NODEJS_HARDWARE_STATE_H