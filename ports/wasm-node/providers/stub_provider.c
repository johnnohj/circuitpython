#include "../hal_provider.h"
#include "py/runtime.h"

// Stub provider - no-op implementation for testing

static void stub_digital_set_direction(hal_pin_t *pin, bool output) {
    // No-op: stub implementation
    (void)pin;
    (void)output;
}

static void stub_digital_set_value(hal_pin_t *pin, bool value) {
    // No-op: stub implementation  
    (void)pin;
    (void)value;
}

static bool stub_digital_get_value(hal_pin_t *pin) {
    (void)pin;
    return false;  // Always return false
}

static uint16_t stub_analog_read(hal_pin_t *pin) {
    (void)pin;
    return 0;  // Always return 0
}

static void stub_analog_write(hal_pin_t *pin, uint16_t value) {
    // No-op: stub implementation
    (void)pin;
    (void)value;
}

static const hal_pin_ops_t stub_pin_ops = {
    .digital_set_direction = stub_digital_set_direction,
    .digital_set_value = stub_digital_set_value,
    .digital_get_value = stub_digital_get_value,
    .digital_set_pull = NULL,
    .analog_read = stub_analog_read,
    .analog_write = stub_analog_write,
    .pin_deinit = NULL,
};

static bool stub_provider_init(void) {
    return true;  // Always succeeds
}

static void stub_provider_deinit(void) {
    // No cleanup needed
}

static mp_obj_t stub_get_board_module(void) {
    // Return None - no board pins defined
    return mp_const_none;
}

const hal_provider_t hal_stub_provider = {
    .name = "stub",
    .capabilities = HAL_CAP_DIGITAL_IO | HAL_CAP_ANALOG_IN | HAL_CAP_ANALOG_OUT,
    .pin_ops = &stub_pin_ops,
    .i2c_ops = NULL,
    .spi_ops = NULL,
    .init = stub_provider_init,
    .deinit = stub_provider_deinit,
    .get_board_module = stub_get_board_module,
};