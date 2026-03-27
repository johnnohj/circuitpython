/*
 * board/__init__.c — Default bus instances for WASI port.
 *
 * Default I2C/SPI/UART creation using the standard pin assignments.
 * When CIRCUITPY_BUSIO is enabled, these will create real bus objects.
 * The reactor can override pin assignments via /hw/board/config.json.
 */

#include "shared-bindings/board/__init__.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/runtime.h"

/* Default bus singletons — lazily created on first board.I2C() etc. */
static mp_obj_t _default_i2c = MP_OBJ_NULL;
static mp_obj_t _default_spi = MP_OBJ_NULL;

bool common_hal_board_is_i2c(mp_obj_t obj) {
    return obj == _default_i2c && _default_i2c != MP_OBJ_NULL;
}

mp_obj_t common_hal_board_get_i2c(const mp_int_t instance) {
    return _default_i2c;
}

mp_obj_t common_hal_board_create_i2c(const mp_int_t instance) {
    #if CIRCUITPY_BUSIO
    if (_default_i2c != MP_OBJ_NULL) {
        return _default_i2c;
    }
    /* TODO: create default I2C on SDA=GPIO8, SCL=GPIO9 */
    #endif
    mp_raise_NotImplementedError(MP_ERROR_TEXT("No default I2C bus"));
}

bool common_hal_board_is_spi(mp_obj_t obj) {
    return obj == _default_spi && _default_spi != MP_OBJ_NULL;
}

mp_obj_t common_hal_board_get_spi(const mp_int_t instance) {
    return _default_spi;
}

mp_obj_t common_hal_board_create_spi(const mp_int_t instance) {
    #if CIRCUITPY_BUSIO
    if (_default_spi != MP_OBJ_NULL) {
        return _default_spi;
    }
    /* TODO: create default SPI on SCK=GPIO12, MOSI=GPIO10, MISO=GPIO11 */
    #endif
    mp_raise_NotImplementedError(MP_ERROR_TEXT("No default SPI bus"));
}

bool common_hal_board_is_uart(mp_obj_t obj) {
    return false;
}

mp_obj_t common_hal_board_get_uart(const mp_int_t instance) {
    return MP_OBJ_NULL;
}

mp_obj_t common_hal_board_create_uart(const mp_int_t instance) {
    mp_raise_NotImplementedError(MP_ERROR_TEXT("No default UART bus"));
}
