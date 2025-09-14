// Generic Metro board HAL provider implementation
// Provides a fully-featured simulated board when no hardware is available

#include "../hal_provider.h"
#include "../generic_board.h"
#include <emscripten.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// JavaScript callbacks for visual updates
EM_JS(void, js_notify_pin_change, (const char* pin_name, int value), {
    const name = UTF8ToString(pin_name);
    if (Module.onGenericPinChange) {
        Module.onGenericPinChange(name, value);
    }
    
    // Special handling for common pins
    if (name === 'LED' || name === 'D13') {
        if (Module.onLEDChange) {
            Module.onLEDChange(value);
        }
    }
});

EM_JS(int, js_get_button_state, (void), {
    if (Module.getButtonState) {
        return Module.getButtonState() ? 1 : 0;
    }
    return 1;  // Default to not pressed (pull-up)
});

EM_JS(float, js_get_analog_value, (const char* pin_name), {
    const name = UTF8ToString(pin_name);
    if (Module.getAnalogValue) {
        return Module.getAnalogValue(name);
    }
    // Return a default analog value (middle of range)
    return 512.0;  // 10-bit ADC middle value
});

// Generic provider implementation
static bool generic_init(void) {
    printf("Generic Metro HAL provider initialized\n");
    generic_board_init();
    generic_board_apply_config();
    return true;
}

static void generic_deinit(void) {
    // Nothing to clean up for now
}

// Pin operations
static void generic_digital_set_direction(hal_pin_t *pin, bool output) {
    if (!pin) return;
    
    printf("Generic: Pin %s direction set to %s\n", 
           pin->name, output ? "OUTPUT" : "INPUT");
    
    // Call JavaScript function to update pin direction
    mp_js_generic_pin_set_direction(pin->name, output ? 1 : 0);
}

static void generic_digital_set_value(hal_pin_t *pin, bool value) {
    if (!pin || !pin->name) return;
    
    // Update internal state
    mp_js_generic_pin_set_value(pin->name, value ? 1 : 0);
    
    // Notify JavaScript for visual updates
    js_notify_pin_change(pin->name, value ? 1 : 0);
}

static bool generic_digital_get_value(hal_pin_t *pin) {
    if (!pin || !pin->name) return false;
    
    // Special handling for button
    if (strcmp(pin->name, "BUTTON") == 0) {
        return js_get_button_state() != 0;
    }
    
    // Get value from internal state
    int value = mp_js_generic_pin_get_value(pin->name);
    return value != 0;
}

static void generic_digital_set_pull(hal_pin_t *pin, int pull_mode) {
    if (!pin) return;
    
    const char *pull_str;
    switch (pull_mode) {
        case 0:  // No pull
            pull_str = "NONE";
            break;
        case 1:  // Pull up
            pull_str = "UP";
            break;
        case 2:  // Pull down
            pull_str = "DOWN";
            break;
        default:
            pull_str = "UNKNOWN";
    }
    
    printf("Generic: Pin %s pull set to %s\n", pin->name, pull_str);
}

// Analog operations
static uint16_t generic_analog_read(hal_pin_t *pin) {
    if (!pin || !pin->name) return 0;
    
    // Check if pin has analog capability
    for (int i = 0; i < GENERIC_METRO_PIN_COUNT; i++) {
        if (strcmp(generic_metro_pins[i].name, pin->name) == 0) {
            if (generic_metro_pins[i].capabilities & PIN_CAP_ANALOG_IN) {
                // Get analog value from JavaScript or return simulated value
                float value = js_get_analog_value(pin->name);
                return (uint16_t)value;
            }
            break;
        }
    }
    
    return 0;  // Pin doesn't support analog
}

static void generic_analog_write(hal_pin_t *pin, uint16_t value) {
    if (!pin || !pin->name) return;
    
    // Check if pin has PWM capability
    for (int i = 0; i < GENERIC_METRO_PIN_COUNT; i++) {
        if (strcmp(generic_metro_pins[i].name, pin->name) == 0) {
            if (generic_metro_pins[i].capabilities & PIN_CAP_PWM) {
                printf("Generic: PWM on pin %s set to %d\n", pin->name, value);
                js_notify_pin_change(pin->name, value);
                return;
            }
            break;
        }
    }
}

static void generic_pin_deinit(hal_pin_t *pin) {
    if (!pin) return;
    printf("Generic: Pin %s deinitialized\n", pin->name);
}

// I2C operations (using CircuitPython-style interface)
static mp_obj_t generic_i2c_create(mp_obj_t scl_pin, mp_obj_t sda_pin, uint32_t frequency) {
    printf("Generic: I2C created (SCL=%p, SDA=%p, freq=%u)\n",
           scl_pin, sda_pin, (unsigned int)frequency);
    // Return a dummy I2C object for now
    return mp_const_none;
}

static bool generic_i2c_try_lock(mp_obj_t i2c_obj) {
    printf("Generic: I2C try_lock\n");
    return true;
}

static void generic_i2c_unlock(mp_obj_t i2c_obj) {
    printf("Generic: I2C unlock\n");
}

static void generic_i2c_scan(mp_obj_t i2c_obj, uint8_t *addresses, size_t *count) {
    printf("Generic: I2C scan\n");
    // Return a dummy device at address 0x3C (common OLED address)
    if (addresses && count) {
        addresses[0] = 0x3C;
        *count = 1;
    }
}

static void generic_i2c_writeto(mp_obj_t i2c_obj, uint8_t addr, const uint8_t *data, size_t len) {
    printf("Generic: I2C writeto 0x%02x (%zu bytes)\n", addr, len);
}

static void generic_i2c_readfrom(mp_obj_t i2c_obj, uint8_t addr, uint8_t *data, size_t len) {
    printf("Generic: I2C readfrom 0x%02x (%zu bytes)\n", addr, len);
    // Return dummy data
    if (data) {
        for (size_t i = 0; i < len; i++) {
            data[i] = 0x55;
        }
    }
}

static void generic_i2c_deinit(mp_obj_t i2c_obj) {
    printf("Generic: I2C deinit\n");
}

// SPI operations (using CircuitPython-style interface)
static mp_obj_t generic_spi_create(mp_obj_t clk_pin, mp_obj_t mosi_pin, mp_obj_t miso_pin) {
    printf("Generic: SPI created (CLK=%p, MOSI=%p, MISO=%p)\n",
           clk_pin, mosi_pin, miso_pin);
    return mp_const_none;
}

static void generic_spi_configure(mp_obj_t spi_obj, uint32_t baudrate, uint8_t polarity, uint8_t phase) {
    printf("Generic: SPI configure (rate=%u, pol=%u, phase=%u)\n", 
           (unsigned int)baudrate, polarity, phase);
}

static bool generic_spi_try_lock(mp_obj_t spi_obj) {
    printf("Generic: SPI try_lock\n");
    return true;
}

static void generic_spi_unlock(mp_obj_t spi_obj) {
    printf("Generic: SPI unlock\n");
}

static void generic_spi_write(mp_obj_t spi_obj, const uint8_t *data, size_t len) {
    printf("Generic: SPI write (%zu bytes)\n", len);
}

static void generic_spi_readinto(mp_obj_t spi_obj, uint8_t *buffer, size_t len) {
    printf("Generic: SPI readinto (%zu bytes)\n", len);
    // Return dummy data
    if (buffer) {
        for (size_t i = 0; i < len; i++) {
            buffer[i] = 0xAA;
        }
    }
}

static void generic_spi_deinit(mp_obj_t spi_obj) {
    printf("Generic: SPI deinit\n");
}

// Pin operations structure
static const hal_pin_ops_t generic_pin_ops = {
    .digital_set_direction = generic_digital_set_direction,
    .digital_set_value = generic_digital_set_value,
    .digital_get_value = generic_digital_get_value,
    .digital_set_pull = generic_digital_set_pull,
    .analog_read = generic_analog_read,
    .analog_write = generic_analog_write,
    .pin_deinit = generic_pin_deinit,
};

// I2C operations structure
static const hal_i2c_ops_t generic_i2c_ops = {
    .i2c_create = generic_i2c_create,
    .i2c_try_lock = generic_i2c_try_lock,
    .i2c_unlock = generic_i2c_unlock,
    .i2c_scan = generic_i2c_scan,
    .i2c_writeto = generic_i2c_writeto,
    .i2c_readfrom = generic_i2c_readfrom,
    .i2c_deinit = generic_i2c_deinit,
};

// SPI operations structure
static const hal_spi_ops_t generic_spi_ops = {
    .spi_create = generic_spi_create,
    .spi_configure = generic_spi_configure,
    .spi_try_lock = generic_spi_try_lock,
    .spi_unlock = generic_spi_unlock,
    .spi_write = generic_spi_write,
    .spi_readinto = generic_spi_readinto,
    .spi_deinit = generic_spi_deinit,
};

// Board module generation
static mp_obj_t generic_get_board_module(void) {
    printf("Generic: get_board_module called\n");
    // TODO: Create a proper board module with pin definitions
    return mp_const_none;
}

// Generic Metro provider definition
const hal_provider_t hal_generic_provider = {
    .name = "generic_metro",
    .capabilities = HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT | 
                   HAL_CAP_I2C | HAL_CAP_SPI | HAL_CAP_PWM,
    .init = generic_init,
    .deinit = generic_deinit,
    .pin_ops = &generic_pin_ops,
    .i2c_ops = &generic_i2c_ops,
    .spi_ops = &generic_spi_ops,
    .get_board_module = generic_get_board_module,
};