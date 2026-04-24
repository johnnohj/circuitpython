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

/* ---- Pin category initialization ---- */
#include "supervisor/hal.h"

/* Classify a board qstr name into a HAL category.
 * Uses qstr comparison against well-known prefixes/names. */
static uint8_t _classify_qstr(qstr q) {
    /* Exact matches first (highest specificity) */
    if (q == MP_QSTR_LED)      return HAL_CAT_LED;
    if (q == MP_QSTR_NEOPIXEL) return HAL_CAT_NEOPIXEL;
    if (q == MP_QSTR_SDA || q == MP_QSTR_SCL) return HAL_CAT_BUS_I2C;
    if (q == MP_QSTR_MOSI || q == MP_QSTR_MISO || q == MP_QSTR_SCK)
        return HAL_CAT_BUS_SPI;
    if (q == MP_QSTR_TX || q == MP_QSTR_RX) return HAL_CAT_BUS_UART;

    /* Prefix-based: check the string representation */
    const char *s = qstr_str(q);
    if (s[0] == 'B' && s[1] == 'U' && s[2] == 'T' && s[3] == 'T')
        return HAL_CAT_BUTTON;   /* BUTTON_A, BUTTON_B, BUTTON */
    if (s[0] == 'A' && s[1] >= '0' && s[1] <= '9')
        return HAL_CAT_ANALOG;   /* A0-A9 */
    if (s[0] == 'D' && s[1] >= '0' && s[1] <= '9')
        return HAL_CAT_DIGITAL;  /* D0-D13 */

    return HAL_CAT_NONE;
}

/* Walk the static board table and populate pin_meta categories.
 * Higher-specificity categories win when multiple names map to one GPIO. */
void hal_init_pin_categories(void) {
    mp_map_t *map = &board_module_globals.map;
    for (size_t i = 0; i < map->alloc; i++) {
        mp_map_elem_t *elem = &map->table[i];
        if (elem->key == MP_OBJ_NULL) continue;
        if (!mp_obj_is_type(elem->value, &mcu_pin_type)) continue;

        const mcu_pin_obj_t *pin = MP_OBJ_TO_PTR(elem->value);
        qstr q = MP_OBJ_QSTR_VALUE(elem->key);
        uint8_t cat = _classify_qstr(q);

        /* Higher specificity wins (BUTTON > DIGITAL, BUS > ANALOG) */
        if (cat > hal_get_category(pin->number)) {
            hal_set_category(pin->number, cat);
        }
    }
}
