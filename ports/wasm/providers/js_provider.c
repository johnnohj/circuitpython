#include "../hal_provider.h"
#include "../generic_board.h"
#include "py/runtime.h"
#include "emscripten.h"
#include <string.h>
#include <stdio.h>

// JavaScript Hardware Provider for HAL with Generic Board support

// Enhanced JavaScript bridge functions with pin name support
EM_JS(void, js_digital_set_direction_by_name, (const char* pin_name, bool output), {
    const name = UTF8ToString(pin_name);
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetDirection) {
        Module.hardwareProvider.digitalSetDirection(name, output);
    } else {
        console.log(`HAL: Pin ${name} direction -> ${output ? 'OUTPUT' : 'INPUT'}`);
    }
});

// Legacy number-based function for compatibility
EM_JS(void, js_digital_set_direction, (int pin, bool output), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetDirectionByNumber) {
        Module.hardwareProvider.digitalSetDirectionByNumber(pin, output);
    } else {
        console.log(`HAL: Pin #${pin} direction -> ${output ? 'OUTPUT' : 'INPUT'}`);
    }
});

EM_JS(void, js_digital_set_value_by_name, (const char* pin_name, bool value), {
    const name = UTF8ToString(pin_name);
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetValue) {
        Module.hardwareProvider.digitalSetValue(name, value);
    } else {
        console.log(`HAL: Pin ${name} = ${value}`);
    }
    
    // Special handling for common pins
    if (name === 'LED' || name === 'D13') {
        if (Module.onLEDChange) {
            Module.onLEDChange(value);
        }
    }
    
    // Generic pin change notification
    if (Module.onPinChange) {
        Module.onPinChange(name, value);
    }
});

EM_JS(void, js_digital_set_value, (int pin, bool value), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetValueByNumber) {
        Module.hardwareProvider.digitalSetValueByNumber(pin, value);
    } else {
        console.log(`HAL: Pin #${pin} = ${value}`);
    }
});

EM_JS(bool, js_digital_get_value_by_name, (const char* pin_name), {
    const name = UTF8ToString(pin_name);
    if (Module.hardwareProvider && Module.hardwareProvider.digitalGetValue) {
        return Module.hardwareProvider.digitalGetValue(name);
    }
    
    // Special handling for button
    if (name === 'BUTTON' && Module.getButtonState) {
        return Module.getButtonState() ? 0 : 1;  // Inverted logic for pull-up
    }
    
    return false;
});

EM_JS(bool, js_digital_get_value, (int pin), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalGetValueByNumber) {
        return Module.hardwareProvider.digitalGetValueByNumber(pin);
    }
    return false;
});

EM_JS(int, js_analog_read_by_name, (const char* pin_name), {
    const name = UTF8ToString(pin_name);
    if (Module.hardwareProvider && Module.hardwareProvider.analogRead) {
        return Module.hardwareProvider.analogRead(name);
    }
    
    // Check if JavaScript wants to provide analog value
    if (Module.getAnalogValue) {
        return Module.getAnalogValue(name);
    }
    
    return Math.floor(Math.random() * 1024);  // 10-bit ADC simulation
});

EM_JS(int, js_analog_read, (int pin), {
    if (Module.hardwareProvider && Module.hardwareProvider.analogReadByNumber) {
        return Module.hardwareProvider.analogReadByNumber(pin);
    }
    return Math.floor(Math.random() * 1024);  // 10-bit ADC simulation
});

EM_JS(void, js_analog_write_by_name, (const char* pin_name, int value), {
    const name = UTF8ToString(pin_name);
    if (Module.hardwareProvider && Module.hardwareProvider.analogWrite) {
        Module.hardwareProvider.analogWrite(name, value);
    } else {
        console.log(`HAL: PWM pin ${name} = ${value}`);
    }
    
    // Notify of PWM change
    if (Module.onPWMChange) {
        Module.onPWMChange(name, value);
    }
});

EM_JS(void, js_analog_write, (int pin, int value), {
    if (Module.hardwareProvider && Module.hardwareProvider.analogWriteByNumber) {
        Module.hardwareProvider.analogWriteByNumber(pin, value);
    } else {
        console.log(`HAL: PWM pin #${pin} = ${value}`);
    }
});

// JavaScript callbacks for board initialization
EM_JS(void, js_notify_board_init, (const char* board_json), {
    if (Module.onBoardInit) {
        const json = UTF8ToString(board_json);
        Module.onBoardInit(JSON.parse(json));
    }
});

// HAL JavaScript provider implementation
static void js_provider_digital_set_direction(hal_pin_t *pin, bool output) {
    // Try name-based first if available
    if (pin->name) {
        js_digital_set_direction_by_name(pin->name, output);
    } else {
        js_digital_set_direction(pin->number, output);
    }
}

static void js_provider_digital_set_value(hal_pin_t *pin, bool value) {
    if (pin->name) {
        js_digital_set_value_by_name(pin->name, value);
    } else {
        js_digital_set_value(pin->number, value);
    }
}

static bool js_provider_digital_get_value(hal_pin_t *pin) {
    if (pin->name) {
        return js_digital_get_value_by_name(pin->name);
    } else {
        return js_digital_get_value(pin->number);
    }
}

static uint16_t js_provider_analog_read(hal_pin_t *pin) {
    if (pin->name) {
        return (uint16_t)js_analog_read_by_name(pin->name);
    } else {
        return (uint16_t)js_analog_read(pin->number);
    }
}

static void js_provider_analog_write(hal_pin_t *pin, uint16_t value) {
    if (pin->name) {
        js_analog_write_by_name(pin->name, (int)value);
    } else {
        js_analog_write(pin->number, (int)value);
    }
}

static const hal_pin_ops_t js_pin_ops = {
    .digital_set_direction = js_provider_digital_set_direction,
    .digital_set_value = js_provider_digital_set_value,
    .digital_get_value = js_provider_digital_get_value,
    .digital_set_pull = NULL,  // Not implemented yet
    .analog_read = js_provider_analog_read,
    .analog_write = js_provider_analog_write,
    .pin_deinit = NULL,
};

static bool js_provider_init(void) {
    printf("JavaScript HAL provider initialized\n");
    
    // Initialize the generic board configuration
    generic_board_init();
    
    // Notify JavaScript about board configuration
    const char* board_json = generic_board_to_json();
    if (board_json) {
        js_notify_board_init(board_json);
    }
    
    // Register pins from generic board
    for (int i = 0; i < GENERIC_BOARD_PIN_COUNT; i++) {
        hal_pin_create(
            i,  // Use index as pin number for now
            generic_board_pins[i].name,
            generic_board_pins[i].capabilities
        );
    }
    
    return true;
}

static void js_provider_deinit(void) {
    // Clean up any allocated resources
    printf("JavaScript HAL provider deinitialized\n");
}

static mp_obj_t js_get_board_module(void) {
    // Create a board module with pin definitions from generic board
    mp_obj_t board_module = mp_obj_new_module(MP_QSTR_board);
    mp_obj_t module_dict = MP_OBJ_FROM_PTR(mp_obj_module_get_globals(board_module));
    
    // Add pin definitions
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

const hal_provider_t hal_js_provider = {
    .name = "javascript",
    .capabilities = HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT | HAL_CAP_PWM,
    .pin_ops = &js_pin_ops,
    .i2c_ops = NULL,  // Could be implemented
    .spi_ops = NULL,  // Could be implemented
    .init = js_provider_init,
    .deinit = js_provider_deinit,
    .get_board_module = js_get_board_module,
};

// Export for JavaScript registration
EMSCRIPTEN_KEEPALIVE void hal_register_js_provider(void) {
    hal_register_provider(&hal_js_provider);
}