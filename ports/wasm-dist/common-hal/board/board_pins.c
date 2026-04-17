/*
 * board_pins.c — Board pin name mapping for WASM browser board.
 *
 * Maps user-friendly names (D0, A0, LED, SDA, SCK, etc.) to
 * the underlying GPIO pin objects.
 *
 * IMPORTANT: GPIO assignments here MUST match the "id" fields in
 * boards/wasm_browser/definition.json.  That JSON is the single
 * source of truth for the board layout — visual components, the
 * JS hardware adapter, and this C pin table all consume it.
 *
 *   D0-D13   → GPIO  0-13  (digital I/O)
 *   A0-A5    → GPIO 14-19  (analog, separate GPIOs from D0-D5)
 *   NEOPIXEL → GPIO 20
 *   BUTTON_A → GPIO 21
 *   BUTTON_B → GPIO 22
 *
 * Bus pins are aliases into the above ranges:
 *   SDA/SCL  → A4/A5 (GPIO 18/19)  — I2C bus 0
 *   MOSI     → D11   (GPIO 11)     — SPI bus 0
 *   MISO     → D12   (GPIO 12)
 *   SCK      → D13   (GPIO 13)
 *   TX/RX    → D1/D0 (GPIO 1/0)    — UART bus 0
 *   LED      → D13   (GPIO 13)     — built-in LED
 *
 * Changing these assignments requires a recompile.  See README.md
 * for instructions on customizing the board layout.
 */

#include "shared-bindings/board/__init__.h"
#include "common-hal/microcontroller/Pin.h"

#if CIRCUITPY_BUSIO
#include "shared-bindings/busio/I2C.h"
#include "shared-bindings/busio/SPI.h"
#include "shared-bindings/busio/UART.h"
#endif

static const mp_rom_map_elem_t board_module_globals_table[] = {
    CIRCUITPYTHON_BOARD_DICT_STANDARD_ITEMS

    /* Digital pins: D0-D13 → GPIO 0-13 */
    { MP_ROM_QSTR(MP_QSTR_D0),  MP_ROM_PTR(&pin_GPIO0) },
    { MP_ROM_QSTR(MP_QSTR_D1),  MP_ROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_D2),  MP_ROM_PTR(&pin_GPIO2) },
    { MP_ROM_QSTR(MP_QSTR_D3),  MP_ROM_PTR(&pin_GPIO3) },
    { MP_ROM_QSTR(MP_QSTR_D4),  MP_ROM_PTR(&pin_GPIO4) },
    { MP_ROM_QSTR(MP_QSTR_D5),  MP_ROM_PTR(&pin_GPIO5) },
    { MP_ROM_QSTR(MP_QSTR_D6),  MP_ROM_PTR(&pin_GPIO6) },
    { MP_ROM_QSTR(MP_QSTR_D7),  MP_ROM_PTR(&pin_GPIO7) },
    { MP_ROM_QSTR(MP_QSTR_D8),  MP_ROM_PTR(&pin_GPIO8) },
    { MP_ROM_QSTR(MP_QSTR_D9),  MP_ROM_PTR(&pin_GPIO9) },
    { MP_ROM_QSTR(MP_QSTR_D10), MP_ROM_PTR(&pin_GPIO10) },
    { MP_ROM_QSTR(MP_QSTR_D11), MP_ROM_PTR(&pin_GPIO11) },
    { MP_ROM_QSTR(MP_QSTR_D12), MP_ROM_PTR(&pin_GPIO12) },
    { MP_ROM_QSTR(MP_QSTR_D13), MP_ROM_PTR(&pin_GPIO13) },

    /* Analog pins: A0-A5 → GPIO 14-19 (separate from D0-D5) */
    { MP_ROM_QSTR(MP_QSTR_A0), MP_ROM_PTR(&pin_GPIO14) },
    { MP_ROM_QSTR(MP_QSTR_A1), MP_ROM_PTR(&pin_GPIO15) },
    { MP_ROM_QSTR(MP_QSTR_A2), MP_ROM_PTR(&pin_GPIO16) },
    { MP_ROM_QSTR(MP_QSTR_A3), MP_ROM_PTR(&pin_GPIO17) },
    { MP_ROM_QSTR(MP_QSTR_A4), MP_ROM_PTR(&pin_GPIO18) },
    { MP_ROM_QSTR(MP_QSTR_A5), MP_ROM_PTR(&pin_GPIO19) },

    /* I2C bus 0: SDA=A4(GPIO18), SCL=A5(GPIO19) */
    { MP_ROM_QSTR(MP_QSTR_SDA), MP_ROM_PTR(&pin_GPIO18) },
    { MP_ROM_QSTR(MP_QSTR_SCL), MP_ROM_PTR(&pin_GPIO19) },

    /* SPI bus 0: MOSI=D11(GPIO11), MISO=D12(GPIO12), SCK=D13(GPIO13) */
    { MP_ROM_QSTR(MP_QSTR_MOSI), MP_ROM_PTR(&pin_GPIO11) },
    { MP_ROM_QSTR(MP_QSTR_MISO), MP_ROM_PTR(&pin_GPIO12) },
    { MP_ROM_QSTR(MP_QSTR_SCK),  MP_ROM_PTR(&pin_GPIO13) },

    /* Built-in LED: D13 (GPIO 13) */
    { MP_ROM_QSTR(MP_QSTR_LED), MP_ROM_PTR(&pin_GPIO13) },

    /* UART bus 0: TX=D1(GPIO1), RX=D0(GPIO0) */
    { MP_ROM_QSTR(MP_QSTR_TX), MP_ROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_RX), MP_ROM_PTR(&pin_GPIO0) },

    /* NeoPixel: GPIO 20 */
    { MP_ROM_QSTR(MP_QSTR_NEOPIXEL), MP_ROM_PTR(&pin_GPIO20) },

    /* Buttons: BUTTON_A=GPIO21, BUTTON_B=GPIO22 */
    { MP_ROM_QSTR(MP_QSTR_BUTTON_A), MP_ROM_PTR(&pin_GPIO21) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_B), MP_ROM_PTR(&pin_GPIO22) },
    /* Legacy alias: BUTTON → BUTTON_A */
    { MP_ROM_QSTR(MP_QSTR_BUTTON),   MP_ROM_PTR(&pin_GPIO21) },

    /* Bus convenience constructors: board.I2C(), board.SPI(), board.UART()
     * These create lazily-initialized singletons using the default pins
     * defined in mpconfigvariant.h (CIRCUITPY_BOARD_I2C_PIN, etc.).
     * Requires CIRCUITPY_BUSIO=1. */
    { MP_ROM_QSTR(MP_QSTR_I2C),  MP_ROM_PTR(&board_i2c_obj) },
    { MP_ROM_QSTR(MP_QSTR_SPI),  MP_ROM_PTR(&board_spi_obj) },
    { MP_ROM_QSTR(MP_QSTR_UART), MP_ROM_PTR(&board_uart_obj) },
};
MP_DEFINE_CONST_DICT(board_module_globals, board_module_globals_table);
