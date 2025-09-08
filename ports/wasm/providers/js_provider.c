#include "../hal_provider.h"
#include "py/runtime.h"
#include "emscripten.h"

// JavaScript Hardware Provider for HAL

// JavaScript bridge functions using EM_JS
EM_JS(void, js_digital_set_direction, (int pin, bool output), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetDirection) {
        Module.hardwareProvider.digitalSetDirection(pin, output);
    } else {
        console.log(`HAL: Pin ${pin} direction -> ${output ? 'OUTPUT' : 'INPUT'}`);
    }
});

EM_JS(void, js_digital_set_value, (int pin, bool value), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalSetValue) {
        Module.hardwareProvider.digitalSetValue(pin, value);
    } else {
        console.log(`HAL: Pin ${pin} = ${value}`);
    }
});

EM_JS(bool, js_digital_get_value, (int pin), {
    if (Module.hardwareProvider && Module.hardwareProvider.digitalGetValue) {
        return Module.hardwareProvider.digitalGetValue(pin);
    }
    return false;
});

EM_JS(int, js_analog_read, (int pin), {
    if (Module.hardwareProvider && Module.hardwareProvider.analogRead) {
        return Module.hardwareProvider.analogRead(pin);
    }
    return Math.floor(Math.random() * 65536);  // Random value for simulation
});

EM_JS(void, js_analog_write, (int pin, int value), {
    if (Module.hardwareProvider && Module.hardwareProvider.analogWrite) {
        Module.hardwareProvider.analogWrite(pin, value);
    } else {
        console.log(`HAL: Analog pin ${pin} = ${value}`);
    }
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

static uint16_t js_provider_analog_read(hal_pin_t *pin) {
    return (uint16_t)js_analog_read(pin->number);
}

static void js_provider_analog_write(hal_pin_t *pin, uint16_t value) {
    js_analog_write(pin->number, (int)value);
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
    return true;  // Always succeeds - JavaScript handles initialization
}

static void js_provider_deinit(void) {
    // JavaScript handles cleanup
}

static mp_obj_t js_get_board_module(void) {
    // TODO: Create board module with pin definitions
    return mp_const_none;
}

const hal_provider_t hal_js_provider = {
    .name = "javascript",
    .capabilities = HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT,
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