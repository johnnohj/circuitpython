/*
 * Browser variant — the board with a built-in display.
 *
 * Builds on standard (pystack, stackless, VM abort, hardware modules)
 * and adds the display pipeline:
 *   - displayio + framebufferio + terminalio + fontio
 *   - REPL logo (Blinka sprite)
 *   - status bar
 *   - vectorio + bitmaptools
 *
 * Hardware modules (digitalio, analogio, busio, etc.) are shared
 * between both variants — they come from mpconfigboard.h.
 */

// Start from standard variant (pystack, stackless, yield, hardware)
#include "../standard/mpconfigvariant.h"

// Board config
#include "mpconfigboard.h"

// ── Display ──
// 480x360 gives 80x30 terminal at 6x12 font, scale=1
#define WASM_DISPLAY_WIDTH  480
#define WASM_DISPLAY_HEIGHT 360

#define CIRCUITPY_DISPLAYIO         (1)
#define CIRCUITPY_FRAMEBUFFERIO     (1)
#define CIRCUITPY_TERMINALIO        (1)
#define CIRCUITPY_FONTIO            (1)
#define CIRCUITPY_REPL_LOGO         (1)

// ── Default bus pin assignments (match definition.json) ──

// Default I2C bus: SDA=A4(GPIO18), SCL=A5(GPIO19)
#define CIRCUITPY_BOARD_I2C         (1)
#define CIRCUITPY_BOARD_I2C_PIN     {{ .scl = &pin_GPIO19, .sda = &pin_GPIO18 }}
#define DEFAULT_I2C_BUS_SCL         (&pin_GPIO19)
#define DEFAULT_I2C_BUS_SDA         (&pin_GPIO18)

// Default SPI bus: SCK=D13(GPIO13), MOSI=D11(GPIO11), MISO=D12(GPIO12)
#define CIRCUITPY_BOARD_SPI         (1)
#define CIRCUITPY_BOARD_SPI_PIN     {{ .clock = &pin_GPIO13, .mosi = &pin_GPIO11, .miso = &pin_GPIO12 }}
#define DEFAULT_SPI_BUS_SCK         (&pin_GPIO13)
#define DEFAULT_SPI_BUS_MOSI        (&pin_GPIO11)
#define DEFAULT_SPI_BUS_MISO        (&pin_GPIO12)

// Default UART bus: TX=D1(GPIO1), RX=D0(GPIO0)
#define CIRCUITPY_BOARD_UART        (1)
#define CIRCUITPY_BOARD_UART_PIN    {{ .tx = &pin_GPIO1, .rx = &pin_GPIO0 }}
#define DEFAULT_UART_BUS_TX         (&pin_GPIO1)
#define DEFAULT_UART_BUS_RX         (&pin_GPIO0)

// ── Event-driven REPL for browser frame-budget model ──
#define MICROPY_REPL_EVENT_DRIVEN   (1)

// ── Status bar (rendered in terminal top row) ──
#define CIRCUITPY_STATUS_BAR        (1)

// ── Display utilities (pure shared-module, no common-hal) ──
// ── Rotary encoder ──
#undef CIRCUITPY_ROTARYIO
#define CIRCUITPY_ROTARYIO          (1)

// ── Display utilities (pure shared-module, no common-hal) ──
#undef CIRCUITPY_VECTORIO
#define CIRCUITPY_VECTORIO          (1)
#undef CIRCUITPY_BITMAPTOOLS
#define CIRCUITPY_BITMAPTOOLS       (1)

// ── jsffi (JavaScript FFI via WASM imports) ──
#undef MICROPY_PY_JSFFI
#define MICROPY_PY_JSFFI            (1)
