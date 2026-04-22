/*
 * board_pins.c — Board pin name mapping for WASM browser board.
 *
 * Uses CIRCUITPY_MUTABLE_BOARD: the dict starts with the full default
 * pin table (matching definition.json) but can be replaced at runtime
 * by JS calling board_reset() + board_add_pin() for board switching.
 *
 * Default pin assignments (work without any JS intervention):
 *   D0-D13   → GPIO  0-13  (digital I/O)
 *   A0-A5    → GPIO 14-19  (analog)
 *   NEOPIXEL → GPIO 20
 *   BUTTON_A → GPIO 21,  BUTTON_B → GPIO 22
 *   Bus aliases: SDA/SCL=A4/A5, MOSI/MISO/SCK=D11/D12/D13,
 *                TX/RX=D1/D0, LED=D13
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

/* ---- Default board dict (mutable, full pin table) ---- */

static mp_map_elem_t _board_table[] = {
    CIRCUITPYTHON_MUTABLE_BOARD_DICT_STANDARD_ITEMS

    /* Digital pins: D0-D13 → GPIO 0-13 */
    { MP_ROM_QSTR(MP_QSTR_D0),  MP_OBJ_FROM_PTR(&pin_GPIO0) },
    { MP_ROM_QSTR(MP_QSTR_D1),  MP_OBJ_FROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_D2),  MP_OBJ_FROM_PTR(&pin_GPIO2) },
    { MP_ROM_QSTR(MP_QSTR_D3),  MP_OBJ_FROM_PTR(&pin_GPIO3) },
    { MP_ROM_QSTR(MP_QSTR_D4),  MP_OBJ_FROM_PTR(&pin_GPIO4) },
    { MP_ROM_QSTR(MP_QSTR_D5),  MP_OBJ_FROM_PTR(&pin_GPIO5) },
    { MP_ROM_QSTR(MP_QSTR_D6),  MP_OBJ_FROM_PTR(&pin_GPIO6) },
    { MP_ROM_QSTR(MP_QSTR_D7),  MP_OBJ_FROM_PTR(&pin_GPIO7) },
    { MP_ROM_QSTR(MP_QSTR_D8),  MP_OBJ_FROM_PTR(&pin_GPIO8) },
    { MP_ROM_QSTR(MP_QSTR_D9),  MP_OBJ_FROM_PTR(&pin_GPIO9) },
    { MP_ROM_QSTR(MP_QSTR_D10), MP_OBJ_FROM_PTR(&pin_GPIO10) },
    { MP_ROM_QSTR(MP_QSTR_D11), MP_OBJ_FROM_PTR(&pin_GPIO11) },
    { MP_ROM_QSTR(MP_QSTR_D12), MP_OBJ_FROM_PTR(&pin_GPIO12) },
    { MP_ROM_QSTR(MP_QSTR_D13), MP_OBJ_FROM_PTR(&pin_GPIO13) },

    /* Analog pins: A0-A5 → GPIO 14-19 */
    { MP_ROM_QSTR(MP_QSTR_A0), MP_OBJ_FROM_PTR(&pin_GPIO14) },
    { MP_ROM_QSTR(MP_QSTR_A1), MP_OBJ_FROM_PTR(&pin_GPIO15) },
    { MP_ROM_QSTR(MP_QSTR_A2), MP_OBJ_FROM_PTR(&pin_GPIO16) },
    { MP_ROM_QSTR(MP_QSTR_A3), MP_OBJ_FROM_PTR(&pin_GPIO17) },
    { MP_ROM_QSTR(MP_QSTR_A4), MP_OBJ_FROM_PTR(&pin_GPIO18) },
    { MP_ROM_QSTR(MP_QSTR_A5), MP_OBJ_FROM_PTR(&pin_GPIO19) },

    /* I2C: SDA=A4, SCL=A5 */
    { MP_ROM_QSTR(MP_QSTR_SDA), MP_OBJ_FROM_PTR(&pin_GPIO18) },
    { MP_ROM_QSTR(MP_QSTR_SCL), MP_OBJ_FROM_PTR(&pin_GPIO19) },

    /* SPI: MOSI=D11, MISO=D12, SCK=D13 */
    { MP_ROM_QSTR(MP_QSTR_MOSI), MP_OBJ_FROM_PTR(&pin_GPIO11) },
    { MP_ROM_QSTR(MP_QSTR_MISO), MP_OBJ_FROM_PTR(&pin_GPIO12) },
    { MP_ROM_QSTR(MP_QSTR_SCK),  MP_OBJ_FROM_PTR(&pin_GPIO13) },

    /* LED = D13 */
    { MP_ROM_QSTR(MP_QSTR_LED), MP_OBJ_FROM_PTR(&pin_GPIO13) },

    /* UART: TX=D1, RX=D0 */
    { MP_ROM_QSTR(MP_QSTR_TX), MP_OBJ_FROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_RX), MP_OBJ_FROM_PTR(&pin_GPIO0) },

    /* NeoPixel */
    { MP_ROM_QSTR(MP_QSTR_NEOPIXEL), MP_OBJ_FROM_PTR(&pin_GPIO20) },

    /* Buttons */
    { MP_ROM_QSTR(MP_QSTR_BUTTON_A), MP_OBJ_FROM_PTR(&pin_GPIO21) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_B), MP_OBJ_FROM_PTR(&pin_GPIO22) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON),   MP_OBJ_FROM_PTR(&pin_GPIO21) },

    /* Bus constructors */
    { MP_ROM_QSTR(MP_QSTR_I2C),  MP_OBJ_FROM_PTR(&board_i2c_obj) },
    { MP_ROM_QSTR(MP_QSTR_SPI),  MP_OBJ_FROM_PTR(&board_spi_obj) },
    { MP_ROM_QSTR(MP_QSTR_UART), MP_OBJ_FROM_PTR(&board_uart_obj) },

    /* Display */
    #if CIRCUITPY_DISPLAYIO
    { MP_ROM_QSTR(MP_QSTR_DISPLAY), MP_OBJ_FROM_PTR(&displays[0].framebuffer_display) },
    #endif
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

/* ---- Shared input buffer for pin names ---- */
#include "supervisor/port_memory.h"

/* ---- WASM exports for board switching ---- */

/* Reset the board dict to only standard items.
 * Call before re-populating with a new board definition. */
__attribute__((export_name("board_reset")))
void board_reset(void) {
    mp_obj_dict_t *d = &board_module_globals;
    mp_map_init(&d->map, 2);
    d->map.all_keys_are_qstrs = 1;

    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR___name__),
        MP_OBJ_NEW_QSTR(MP_QSTR_board));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(d),
        MP_OBJ_NEW_QSTR(MP_QSTR_board_id),
        MP_OBJ_FROM_PTR(&board_module_id_obj));
}

/* Add a pin to the board dict by name.
 * JS writes the name into port_input_buf, then calls this. */
__attribute__((export_name("board_add_pin")))
int board_add_pin(int name_len, int gpio_id) {
    if (gpio_id < 0 || gpio_id >= 64) return -1;
    if (name_len <= 0 || name_len >= (int)port_input_buf_size()) return -1;

    char *name = port_input_buf();
    name[name_len] = '\0';

    qstr q = qstr_from_str(name);
    mp_obj_dict_store(
        MP_OBJ_FROM_PTR(&board_module_globals),
        MP_OBJ_NEW_QSTR(q),
        MP_OBJ_FROM_PTR(_gpio_table[gpio_id]));

    return 0;
}

/* Finalize: add bus constructors and display after custom pins. */
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
