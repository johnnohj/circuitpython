#include "nodejs_hardware_state.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Global hardware state - C-side for performance
static nodejs_hardware_state_t g_hardware_state = {0};
static bool g_state_initialized = false;

// Initialize the Node.js hardware state system
void nodejs_hardware_state_init(void) {
    if (g_state_initialized) {
        return;
    }

    memset(&g_hardware_state, 0, sizeof(nodejs_hardware_state_t));

    // Initialize with common automation-friendly defaults
    g_hardware_state.performance_mode = true;  // CLI environments prefer performance

    printf("[Node.js HAL] C-side hardware state initialized for CLI/automation\n");
    g_state_initialized = true;
}

void nodejs_hardware_state_deinit(void) {
    if (!g_state_initialized) {
        return;
    }

    // Print performance stats for CLI debugging
    printf("[Node.js HAL] Performance stats: %u operations, %u JS calls\n",
           g_hardware_state.total_operations,
           g_hardware_state.javascript_calls);

    memset(&g_hardware_state, 0, sizeof(nodejs_hardware_state_t));
    g_state_initialized = false;
}

// Pin state management functions
nodejs_pin_state_t* nodejs_pin_get_state(const char* pin_name) {
    if (!g_state_initialized) {
        nodejs_hardware_state_init();
    }

    for (uint8_t i = 0; i < g_hardware_state.pin_count; i++) {
        if (strcmp(g_hardware_state.pins[i].name, pin_name) == 0) {
            return &g_hardware_state.pins[i];
        }
    }
    return NULL;
}

nodejs_pin_state_t* nodejs_pin_get_or_create_state(const char* pin_name) {
    nodejs_pin_state_t* state = nodejs_pin_get_state(pin_name);
    if (state != NULL) {
        return state;
    }

    // Create new pin state
    if (g_hardware_state.pin_count >= 64) {
        printf("[Node.js HAL] Warning: Maximum pin count reached\n");
        return NULL;
    }

    state = &g_hardware_state.pins[g_hardware_state.pin_count];
    strncpy(state->name, pin_name, sizeof(state->name) - 1);
    state->name[sizeof(state->name) - 1] = '\0';

    // Initialize with safe defaults for automation
    state->digital_value = false;
    state->analog_value = 0;
    state->mode = NODEJS_PIN_MODE_INPUT;
    state->pull = NODEJS_PULL_NONE;
    state->in_use = true;
    state->last_access_time = (uint32_t)time(NULL);
    state->state_dirty = false;

    g_hardware_state.pin_count++;

    printf("[Node.js HAL] Created pin state for %s (total: %u)\n",
           pin_name, g_hardware_state.pin_count);
    return state;
}

void nodejs_pin_set_digital(const char* pin_name, bool value) {
    nodejs_pin_state_t* state = nodejs_pin_get_or_create_state(pin_name);
    if (state == NULL) return;

    state->digital_value = value;
    state->last_access_time = (uint32_t)time(NULL);
    state->state_dirty = true;
    g_hardware_state.total_operations++;

    // Performance mode: batch updates instead of immediate sync
    if (!g_hardware_state.performance_mode) {
        g_hardware_state.javascript_calls++;
        // Immediate sync would happen here in non-performance mode
    }
}

bool nodejs_pin_get_digital(const char* pin_name) {
    nodejs_pin_state_t* state = nodejs_pin_get_or_create_state(pin_name);
    if (state == NULL) return false;

    state->last_access_time = (uint32_t)time(NULL);
    g_hardware_state.total_operations++;

    return state->digital_value;
}

void nodejs_pin_set_analog(const char* pin_name, uint16_t value) {
    nodejs_pin_state_t* state = nodejs_pin_get_or_create_state(pin_name);
    if (state == NULL) return;

    state->analog_value = value;
    state->last_access_time = (uint32_t)time(NULL);
    state->state_dirty = true;
    g_hardware_state.total_operations++;
}

uint16_t nodejs_pin_get_analog(const char* pin_name) {
    nodejs_pin_state_t* state = nodejs_pin_get_or_create_state(pin_name);
    if (state == NULL) return 0;

    state->last_access_time = (uint32_t)time(NULL);
    g_hardware_state.total_operations++;

    // For CLI automation, provide realistic sensor-like values
    if (state->analog_value == 0 && strstr(pin_name, "TEMP") != NULL) {
        // Mock temperature sensor
        state->analog_value = 20000 + (rand() % 10000); // ~3.0-4.5V range
    } else if (state->analog_value == 0 && strstr(pin_name, "LIGHT") != NULL) {
        // Mock light sensor
        state->analog_value = rand() % 65536;
    }

    return state->analog_value;
}

void nodejs_pin_set_mode(const char* pin_name, nodejs_pin_mode_t mode) {
    nodejs_pin_state_t* state = nodejs_pin_get_or_create_state(pin_name);
    if (state == NULL) return;

    state->mode = mode;
    state->state_dirty = true;
    g_hardware_state.total_operations++;
}

void nodejs_pin_set_pull(const char* pin_name, nodejs_pull_mode_t pull) {
    nodejs_pin_state_t* state = nodejs_pin_get_or_create_state(pin_name);
    if (state == NULL) return;

    state->pull = pull;
    state->state_dirty = true;
    g_hardware_state.total_operations++;
}

// I2C state management
int nodejs_i2c_create_bus(uint8_t scl_pin, uint8_t sda_pin, uint32_t frequency) {
    if (g_hardware_state.i2c_count >= 2) {
        printf("[Node.js HAL] Warning: Maximum I2C bus count reached\n");
        return -1;
    }

    int bus_id = g_hardware_state.i2c_count;
    nodejs_i2c_state_t* bus = &g_hardware_state.i2c_buses[bus_id];

    bus->scl_pin = scl_pin;
    bus->sda_pin = sda_pin;
    bus->frequency = frequency;
    bus->locked = false;
    bus->in_use = true;
    bus->device_count = 0;

    // Add some common mock devices for CLI automation
    nodejs_i2c_add_mock_device(bus_id, 0x48); // Temperature sensor
    nodejs_i2c_add_mock_device(bus_id, 0x68); // RTC

    g_hardware_state.i2c_count++;
    printf("[Node.js HAL] I2C bus %d created: SCL=%u, SDA=%u, %uHz\n",
           bus_id, scl_pin, sda_pin, frequency);

    return bus_id;
}

nodejs_i2c_state_t* nodejs_i2c_get_bus(int bus_id) {
    if (bus_id < 0 || bus_id >= g_hardware_state.i2c_count) {
        return NULL;
    }
    return &g_hardware_state.i2c_buses[bus_id];
}

bool nodejs_i2c_try_lock(int bus_id) {
    nodejs_i2c_state_t* bus = nodejs_i2c_get_bus(bus_id);
    if (bus == NULL || bus->locked) {
        return false;
    }

    bus->locked = true;
    return true;
}

void nodejs_i2c_unlock(int bus_id) {
    nodejs_i2c_state_t* bus = nodejs_i2c_get_bus(bus_id);
    if (bus != NULL) {
        bus->locked = false;
    }
}

void nodejs_i2c_add_mock_device(int bus_id, uint8_t address) {
    nodejs_i2c_state_t* bus = nodejs_i2c_get_bus(bus_id);
    if (bus == NULL || bus->device_count >= 8) {
        return;
    }

    bus->device_addresses[bus->device_count] = address;
    bus->device_count++;
}

// SPI state management
int nodejs_spi_create_bus(uint8_t clk_pin, uint8_t mosi_pin, uint8_t miso_pin) {
    if (g_hardware_state.spi_count >= 2) {
        printf("[Node.js HAL] Warning: Maximum SPI bus count reached\n");
        return -1;
    }

    int bus_id = g_hardware_state.spi_count;
    nodejs_spi_state_t* bus = &g_hardware_state.spi_buses[bus_id];

    bus->clk_pin = clk_pin;
    bus->mosi_pin = mosi_pin;
    bus->miso_pin = miso_pin;
    bus->baudrate = 100000; // Default 100kHz
    bus->polarity = 0;
    bus->phase = 0;
    bus->locked = false;
    bus->in_use = true;

    g_hardware_state.spi_count++;
    printf("[Node.js HAL] SPI bus %d created: CLK=%u, MOSI=%u, MISO=%u\n",
           bus_id, clk_pin, mosi_pin, miso_pin);

    return bus_id;
}

nodejs_spi_state_t* nodejs_spi_get_bus(int bus_id) {
    if (bus_id < 0 || bus_id >= g_hardware_state.spi_count) {
        return NULL;
    }
    return &g_hardware_state.spi_buses[bus_id];
}

bool nodejs_spi_try_lock(int bus_id) {
    nodejs_spi_state_t* bus = nodejs_spi_get_bus(bus_id);
    if (bus == NULL || bus->locked) {
        return false;
    }

    bus->locked = true;
    return true;
}

void nodejs_spi_unlock(int bus_id) {
    nodejs_spi_state_t* bus = nodejs_spi_get_bus(bus_id);
    if (bus != NULL) {
        bus->locked = false;
    }
}

void nodejs_spi_configure(int bus_id, uint32_t baudrate, uint8_t polarity, uint8_t phase) {
    nodejs_spi_state_t* bus = nodejs_spi_get_bus(bus_id);
    if (bus != NULL) {
        bus->baudrate = baudrate;
        bus->polarity = polarity;
        bus->phase = phase;
    }
}

// Performance optimization functions
void nodejs_hardware_enable_performance_mode(bool enable) {
    g_hardware_state.performance_mode = enable;
    printf("[Node.js HAL] Performance mode: %s\n", enable ? "enabled" : "disabled");
}

void nodejs_hardware_sync_to_javascript(void) {
    // Batch synchronization of dirty state to JavaScript
    uint32_t synced_pins = 0;

    for (uint8_t i = 0; i < g_hardware_state.pin_count; i++) {
        if (g_hardware_state.pins[i].state_dirty) {
            // Here we would batch call JavaScript to sync the pin state
            g_hardware_state.pins[i].state_dirty = false;
            synced_pins++;
        }
    }

    if (synced_pins > 0) {
        g_hardware_state.javascript_calls++;
        printf("[Node.js HAL] Synced %u pins to JavaScript\n", synced_pins);
    }
}

uint32_t nodejs_hardware_get_stats(void) {
    return g_hardware_state.total_operations;
}

// CLI simulation helpers
void nodejs_hardware_print_status(void) {
    printf("\n[Node.js HAL] Hardware Status:\n");
    printf("  Pins: %u active\n", g_hardware_state.pin_count);
    printf("  I2C buses: %u\n", g_hardware_state.i2c_count);
    printf("  SPI buses: %u\n", g_hardware_state.spi_count);
    printf("  Operations: %u total, %u JS calls\n",
           g_hardware_state.total_operations,
           g_hardware_state.javascript_calls);
    printf("  Performance mode: %s\n",
           g_hardware_state.performance_mode ? "enabled" : "disabled");

    // Show active pins for CLI debugging
    for (uint8_t i = 0; i < g_hardware_state.pin_count && i < 5; i++) {
        nodejs_pin_state_t* pin = &g_hardware_state.pins[i];
        printf("    %s: %s=%s, analog=%u\n",
               pin->name,
               pin->mode == NODEJS_PIN_MODE_OUTPUT ? "OUT" : "IN",
               pin->digital_value ? "HIGH" : "LOW",
               pin->analog_value);
    }
    if (g_hardware_state.pin_count > 5) {
        printf("    ... and %u more pins\n", g_hardware_state.pin_count - 5);
    }
}