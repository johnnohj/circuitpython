/*
 * board_pins.c — Dynamic board pin mapping for WASM port.
 *
 * Uses CIRCUITPY_MUTABLE_BOARD: the board dict starts with only
 * standard items (__name__, board_id) and is populated at runtime
 * by JS calling board_add_pin() for each pin in definition.json.
 *
 * This replaces the static ROM table with a runtime-populated dict,
 * enabling board switching without recompilation.
 *
 * Boot sequence:
 *   1. JS parses definition.json
 *   2. JS calls board_reset() to clear existing pins
 *   3. JS calls board_add_pin(name, len, gpio_id) for each pin
 *   4. JS calls board_add_pin for aliases (LED, SDA, SCL, etc.)
 *   5. board_finalize() adds bus constructors and display
 */

#include "shared-bindings/board/__init__.h"
#include "common-hal/microcontroller/Pin.h"
#include "py/runtime.h"
#include "py/objstr.h"

#if CIRCUITPY_BUSIO
#include "shared-bindings/busio/I2C.h"
#include "shared-bindings/busio/SPI.h"
#include "shared-bindings/busio/UART.h"
#endif

#if CIRCUITPY_DISPLAYIO
#include "shared-module/displayio/__init__.h"
#endif

/* ---- Pin object references (from Pin.c) ---- */
extern const mcu_pin_obj_t pin_GPIO0,  pin_GPIO1,  pin_GPIO2,  pin_GPIO3;
extern const mcu_pin_obj_t pin_GPIO4,  pin_GPIO5,  pin_GPIO6,  pin_GPIO7;
extern const mcu_pin_obj_t pin_GPIO8,  pin_GPIO9,  pin_GPIO10, pin_GPIO11;
extern const mcu_pin_obj_t pin_GPIO12, pin_GPIO13, pin_GPIO14, pin_GPIO15;
extern const mcu_pin_obj_t pin_GPIO16, pin_GPIO17, pin_GPIO18, pin_GPIO19;
extern const mcu_pin_obj_t pin_GPIO20, pin_GPIO21, pin_GPIO22, pin_GPIO23;
extern const mcu_pin_obj_t pin_GPIO24, pin_GPIO25, pin_GPIO26, pin_GPIO27;
extern const mcu_pin_obj_t pin_GPIO28, pin_GPIO29, pin_GPIO30, pin_GPIO31;
extern const mcu_pin_obj_t pin_GPIO32, pin_GPIO33, pin_GPIO34, pin_GPIO35;
extern const mcu_pin_obj_t pin_GPIO36, pin_GPIO37, pin_GPIO38, pin_GPIO39;
extern const mcu_pin_obj_t pin_GPIO40, pin_GPIO41, pin_GPIO42, pin_GPIO43;
extern const mcu_pin_obj_t pin_GPIO44, pin_GPIO45, pin_GPIO46, pin_GPIO47;
extern const mcu_pin_obj_t pin_GPIO48, pin_GPIO49, pin_GPIO50, pin_GPIO51;
extern const mcu_pin_obj_t pin_GPIO52, pin_GPIO53, pin_GPIO54, pin_GPIO55;
extern const mcu_pin_obj_t pin_GPIO56, pin_GPIO57, pin_GPIO58, pin_GPIO59;
extern const mcu_pin_obj_t pin_GPIO60, pin_GPIO61, pin_GPIO62, pin_GPIO63;

static const mcu_pin_obj_t *const _gpio_table[64] = {
    &pin_GPIO0,  &pin_GPIO1,  &pin_GPIO2,  &pin_GPIO3,
    &pin_GPIO4,  &pin_GPIO5,  &pin_GPIO6,  &pin_GPIO7,
    &pin_GPIO8,  &pin_GPIO9,  &pin_GPIO10, &pin_GPIO11,
    &pin_GPIO12, &pin_GPIO13, &pin_GPIO14, &pin_GPIO15,
    &pin_GPIO16, &pin_GPIO17, &pin_GPIO18, &pin_GPIO19,
    &pin_GPIO20, &pin_GPIO21, &pin_GPIO22, &pin_GPIO23,
    &pin_GPIO24, &pin_GPIO25, &pin_GPIO26, &pin_GPIO27,
    &pin_GPIO28, &pin_GPIO29, &pin_GPIO30, &pin_GPIO31,
    &pin_GPIO32, &pin_GPIO33, &pin_GPIO34, &pin_GPIO35,
    &pin_GPIO36, &pin_GPIO37, &pin_GPIO38, &pin_GPIO39,
    &pin_GPIO40, &pin_GPIO41, &pin_GPIO42, &pin_GPIO43,
    &pin_GPIO44, &pin_GPIO45, &pin_GPIO46, &pin_GPIO47,
    &pin_GPIO48, &pin_GPIO49, &pin_GPIO50, &pin_GPIO51,
    &pin_GPIO52, &pin_GPIO53, &pin_GPIO54, &pin_GPIO55,
    &pin_GPIO56, &pin_GPIO57, &pin_GPIO58, &pin_GPIO59,
    &pin_GPIO60, &pin_GPIO61, &pin_GPIO62, &pin_GPIO63,
};

/* ---- Mutable board dict ---- */

/* Initial table with just the standard items.  board_add_pin()
 * grows the dict at runtime.  MP_DEFINE_MUTABLE_DICT requires
 * a non-empty initial table. */
static mp_map_elem_t _board_table[] = {
    CIRCUITPYTHON_MUTABLE_BOARD_DICT_STANDARD_ITEMS
};

mp_obj_dict_t board_module_globals = {
    .base = { .type = &mp_type_dict },
    .map = {
        .all_keys_are_qstrs = 1,
        .is_fixed = 0,
        .is_ordered = 0,
        .used = MP_ARRAY_SIZE(_board_table),
        .alloc = MP_ARRAY_SIZE(_board_table),
        .table = _board_table,
    },
};

/* ---- Shared input buffer for pin names (reuse port's input buf) ---- */
#include "supervisor/port_memory.h"

/* ---- WASM exports ---- */

/* Reset the board dict to only standard items.
 * Call before re-populating with a new definition. */
__attribute__((export_name("board_reset")))
void board_reset(void) {
    /* Clear the dict and re-add standard items. */
    mp_obj_dict_t *d = &board_module_globals;

    /* Re-init as a fresh mutable dict. */
    mp_map_init(&d->map, 2);
    d->map.all_keys_are_qstrs = 1;

    /* Add standard items */
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR___name__),
        MP_OBJ_NEW_QSTR(MP_QSTR_board));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_board_id),
        MP_OBJ_FROM_PTR(&board_module_id_obj));
}

/* Add a pin to the board dict.
 * name_len: length of pin name in sup_input_buf (JS writes it there).
 * gpio_id: GPIO number (0-63).
 * Returns 0 on success, -1 on invalid gpio_id. */
__attribute__((export_name("board_add_pin")))
int board_add_pin(int name_len, int gpio_id) {
    if (gpio_id < 0 || gpio_id >= 64) return -1;
    if (name_len <= 0 || name_len >= (int)port_input_buf_size()) return -1;

    char *name = port_input_buf();
    name[name_len] = '\0';

    /* Create a qstr from the pin name */
    qstr q = qstr_from_str(name);

    /* Store pin_GPIO<gpio_id> under that name */
    mp_obj_dict_store(
        MP_OBJ_FROM_PTR(&board_module_globals),
        MP_OBJ_NEW_QSTR(q),
        MP_OBJ_FROM_PTR(_gpio_table[gpio_id]));

    return 0;
}

/* Finalize the board dict by adding bus constructors and display.
 * Call after all pins have been added. */
__attribute__((export_name("board_finalize")))
void board_finalize(void) {
    mp_obj_dict_t *d = &board_module_globals;

    #if CIRCUITPY_BUSIO
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_I2C), MP_OBJ_FROM_PTR(&board_i2c_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_SPI), MP_OBJ_FROM_PTR(&board_spi_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_UART), MP_OBJ_FROM_PTR(&board_uart_obj));
    #endif

    #if CIRCUITPY_DISPLAYIO
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_DISPLAY),
        MP_OBJ_FROM_PTR(&displays[0].framebuffer_display));
    #endif
}
