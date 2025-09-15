#include "../hal_provider.h"
#include "../generic_board.h"
#include "../nodejs_bridge.h"
#include "../nodejs_hardware_state.h"
#include "py/runtime.h"
#include "py/mperrno.h"
#include <string.h>
#include <stdio.h>

// Node.js Hardware Provider for HAL - Clean Abstraction with Performance
// Uses the abstracted bridge interface instead of direct EM_JS calls
// Optimized for CLI tools, automation scripts, and background processes

// Pin operations implementation using abstracted bridge
static void nodejs_pin_digital_set_direction(hal_pin_t *pin, bool output) {
    if (!nodejs_bridge_digital_set_direction(pin->name, output)) {
        printf("[Node.js Provider] Failed to set direction for pin %s\n", pin->name);
    }
}

static void nodejs_pin_digital_set_value(hal_pin_t *pin, bool value) {
    if (!nodejs_bridge_digital_write(pin->name, value)) {
        printf("[Node.js Provider] Failed to write to pin %s\n", pin->name);
    }
}

static bool nodejs_pin_digital_get_value(hal_pin_t *pin) {
    bool value = false;
    if (!nodejs_bridge_digital_read(pin->name, &value)) {
        printf("[Node.js Provider] Failed to read from pin %s\n", pin->name);
        return false;
    }
    return value;
}

static void nodejs_pin_digital_set_pull(hal_pin_t *pin, int pull_mode) {
    if (!nodejs_bridge_digital_set_pull(pin->name, pull_mode)) {
        printf("[Node.js Provider] Failed to set pull for pin %s\n", pin->name);
    }
}

static uint16_t nodejs_pin_analog_read(hal_pin_t *pin) {
    uint16_t value = 0;
    if (!nodejs_bridge_analog_read(pin->name, &value)) {
        printf("[Node.js Provider] Failed to read analog from pin %s\n", pin->name);
        return 0;
    }
    return value;
}

static void nodejs_pin_analog_write(hal_pin_t *pin, uint16_t value) {
    if (!nodejs_bridge_analog_write(pin->name, value)) {
        printf("[Node.js Provider] Failed to write analog to pin %s\n", pin->name);
    }
}

static void nodejs_pin_deinit(hal_pin_t *pin) {
    // Clean shutdown for CLI applications and automation scripts
    printf("[Node.js Provider] Pin %s deinitialized\n", pin->name);

    // Notify hardware state system
    // In a real implementation, we might save pin state for session restoration
}

// I2C operations implementation using abstracted bridge
static mp_obj_t nodejs_i2c_create_impl(mp_obj_t scl_pin, mp_obj_t sda_pin, uint32_t frequency) {
    // Extract pin names - simplified for now
    const char *scl_name = "SCL";
    const char *sda_name = "SDA";

    // TODO: Extract actual pin names from MicroPython pin objects
    // hal_pin_t *scl = (hal_pin_t*)MP_OBJ_TO_PTR(scl_pin);
    // scl_name = scl->name;

    int bus_id = nodejs_bridge_i2c_create(scl_name, sda_name, frequency);
    if (bus_id < 0) {
        mp_raise_OSError(MP_EIO);
    }

    // Return the bus ID as a MicroPython integer
    return mp_obj_new_int(bus_id);
}

static bool nodejs_provider_i2c_try_lock(mp_obj_t i2c_obj) {
    // Extract bus ID from MicroPython object
    int bus_id = mp_obj_get_int(i2c_obj);
    return nodejs_i2c_try_lock(bus_id);
}

static void nodejs_provider_i2c_unlock(mp_obj_t i2c_obj) {
    int bus_id = mp_obj_get_int(i2c_obj);
    nodejs_i2c_unlock(bus_id);
}

static void nodejs_i2c_scan(mp_obj_t i2c_obj, uint8_t *addresses, size_t *count) {
    int bus_id = mp_obj_get_int(i2c_obj);
    uint8_t max_count = *count;

    if (!nodejs_bridge_i2c_scan(bus_id, addresses, (uint8_t*)count)) {
        *count = 0;
        printf("[Node.js Provider] I2C scan failed on bus %d\n", bus_id);
    }

    // Ensure we don't exceed the provided buffer
    if (*count > max_count) {
        *count = max_count;
    }
}

static void nodejs_i2c_writeto(mp_obj_t i2c_obj, uint8_t addr, const uint8_t *data, size_t len) {
    int bus_id = mp_obj_get_int(i2c_obj);

    if (!nodejs_bridge_i2c_write(bus_id, addr, data, len)) {
        printf("[Node.js Provider] I2C write failed: bus=%d, addr=0x%02X, len=%zu\n",
               bus_id, addr, len);
        mp_raise_OSError(MP_EIO);
    }
}

static void nodejs_i2c_readfrom(mp_obj_t i2c_obj, uint8_t addr, uint8_t *data, size_t len) {
    int bus_id = mp_obj_get_int(i2c_obj);

    if (!nodejs_bridge_i2c_read(bus_id, addr, data, len)) {
        printf("[Node.js Provider] I2C read failed: bus=%d, addr=0x%02X, len=%zu\n",
               bus_id, addr, len);
        mp_raise_OSError(MP_EIO);
    }
}

static void nodejs_i2c_deinit(mp_obj_t i2c_obj) {
    int bus_id = mp_obj_get_int(i2c_obj);
    printf("[Node.js Provider] I2C bus %d deinitialized\n", bus_id);
}

// SPI operations implementation using abstracted bridge
static mp_obj_t nodejs_spi_create_impl(mp_obj_t clk_pin, mp_obj_t mosi_pin, mp_obj_t miso_pin) {
    const char *clk_name = "CLK";
    const char *mosi_name = "MOSI";
    const char *miso_name = "MISO";

    int bus_id = nodejs_bridge_spi_create(clk_name, mosi_name, miso_name);
    if (bus_id < 0) {
        mp_raise_OSError(MP_EIO);
    }

    return mp_obj_new_int(bus_id);
}

static void nodejs_spi_configure_impl(mp_obj_t spi_obj, uint32_t baudrate, uint8_t polarity, uint8_t phase) {
    int bus_id = mp_obj_get_int(spi_obj);

    if (!nodejs_bridge_spi_configure(bus_id, baudrate, polarity, phase)) {
        printf("[Node.js Provider] SPI configure failed on bus %d\n", bus_id);
        mp_raise_OSError(MP_EIO);
    }
}

static bool nodejs_provider_spi_try_lock(mp_obj_t spi_obj) {
    int bus_id = mp_obj_get_int(spi_obj);
    return nodejs_spi_try_lock(bus_id);
}

static void nodejs_provider_spi_unlock(mp_obj_t spi_obj) {
    int bus_id = mp_obj_get_int(spi_obj);
    nodejs_spi_unlock(bus_id);
}

static void nodejs_spi_write(mp_obj_t spi_obj, const uint8_t *data, size_t len) {
    int bus_id = mp_obj_get_int(spi_obj);

    if (!nodejs_bridge_spi_transfer(bus_id, data, NULL, len)) {
        printf("[Node.js Provider] SPI write failed on bus %d\n", bus_id);
        mp_raise_OSError(MP_EIO);
    }
}

static void nodejs_spi_readinto(mp_obj_t spi_obj, uint8_t *buffer, size_t len) {
    int bus_id = mp_obj_get_int(spi_obj);

    if (!nodejs_bridge_spi_transfer(bus_id, NULL, buffer, len)) {
        printf("[Node.js Provider] SPI read failed on bus %d\n", bus_id);
        mp_raise_OSError(MP_EIO);
    }
}

static void nodejs_spi_deinit(mp_obj_t spi_obj) {
    int bus_id = mp_obj_get_int(spi_obj);
    printf("[Node.js Provider] SPI bus %d deinitialized\n", bus_id);
}

// Provider lifecycle management - optimized for CLI and automation
static bool nodejs_provider_init(void) {
    printf("[Node.js Provider] Initializing for CLI/automation environment\n");

    // Initialize the abstracted bridge in hybrid mode for auto-detection
    nodejs_bridge_init(NODEJS_BRIDGE_MODE_HYBRID);

    // Enable CLI output for development and debugging
    nodejs_bridge_enable_cli_output(true);

    // Enable batching for better performance in automation scripts
    nodejs_bridge_enable_batching(true);

    printf("[Node.js Provider] Initialization complete - mode: %s\n",
           nodejs_bridge_has_native_hardware() ? "native hardware" : "simulation");

    return true;
}

static void nodejs_provider_deinit(void) {
    printf("[Node.js Provider] Shutting down Node.js hardware provider\n");

    // Flush any pending operations before shutdown
    nodejs_bridge_flush_operations();

    // Print performance statistics for CLI debugging
    uint32_t call_count = nodejs_bridge_get_call_count();
    uint32_t operations = nodejs_hardware_get_stats();

    printf("[Node.js Provider] Session stats: %u operations, %u JS calls\n",
           operations, call_count);

    // Shutdown the bridge
    nodejs_bridge_deinit();
}

static mp_obj_t nodejs_get_board_module(void) {
    // Create a board module with pin definitions from generic board
    mp_obj_t board_module = mp_obj_new_module(MP_QSTR_board);
    mp_obj_t module_dict = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(board_module));

    // Add pin definitions from generic board
    for (int i = 0; i < GENERIC_BOARD_PIN_COUNT; i++) {
        hal_pin_t *pin = hal_pin_find_by_name(generic_board_pins[i].name);
        if (pin) {
            mp_obj_dict_store(
                module_dict,
                mp_obj_new_str(generic_board_pins[i].name, strlen(generic_board_pins[i].name)),
                MP_OBJ_FROM_PTR(pin)
            );
        }
    }

    // Add board metadata
    mp_obj_dict_store(
        module_dict,
        MP_OBJ_NEW_QSTR(MP_QSTR_board_id),
        mp_obj_new_str(generic_board_info.board_name, strlen(generic_board_info.board_name))
    );

    return board_module;
}

// Operation structure definitions
static const hal_pin_ops_t nodejs_pin_ops = {
    .digital_set_direction = nodejs_pin_digital_set_direction,
    .digital_set_value = nodejs_pin_digital_set_value,
    .digital_get_value = nodejs_pin_digital_get_value,
    .digital_set_pull = nodejs_pin_digital_set_pull,
    .analog_read = nodejs_pin_analog_read,
    .analog_write = nodejs_pin_analog_write,
    .pin_deinit = nodejs_pin_deinit,
};

static const hal_i2c_ops_t nodejs_i2c_ops = {
    .i2c_create = nodejs_i2c_create_impl,
    .i2c_try_lock = nodejs_provider_i2c_try_lock,
    .i2c_unlock = nodejs_provider_i2c_unlock,
    .i2c_scan = nodejs_i2c_scan,
    .i2c_writeto = nodejs_i2c_writeto,
    .i2c_readfrom = nodejs_i2c_readfrom,
    .i2c_deinit = nodejs_i2c_deinit,
};

static const hal_spi_ops_t nodejs_spi_ops = {
    .spi_create = nodejs_spi_create_impl,
    .spi_configure = nodejs_spi_configure_impl,
    .spi_try_lock = nodejs_provider_spi_try_lock,
    .spi_unlock = nodejs_provider_spi_unlock,
    .spi_write = nodejs_spi_write,
    .spi_readinto = nodejs_spi_readinto,
    .spi_deinit = nodejs_spi_deinit,
};

// Node.js HAL Provider - Optimized for CLI, automation, and background processes
const hal_provider_t nodejs_hal_provider = {
    .name = "nodejs",
    .capabilities = HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT |
                   HAL_CAP_I2C | HAL_CAP_SPI | HAL_CAP_PWM,
    .pin_ops = &nodejs_pin_ops,
    .i2c_ops = &nodejs_i2c_ops,
    .spi_ops = &nodejs_spi_ops,
    .init = nodejs_provider_init,
    .deinit = nodejs_provider_deinit,
    .get_board_module = nodejs_get_board_module,
};