/*
 * Browser variant — the board.
 *
 * Builds on standard (pystack, stackless, VM yield) and adds:
 *   - displayio + framebufferio + terminalio + fontio (supervisor terminal)
 *   - common-hal modules (digitalio, analogio, pwmio, neopixel_write)
 *   - board definition (virtual GPIO pins)
 *   - REPL logo (Blinka sprite)
 *
 * This is what runs in the browser via cp_step().
 * The standard variant remains for headless CLI testing.
 */

// Start from standard variant (pystack, stackless, yield)
#include "../standard/mpconfigvariant.h"

// Board config — pin definitions, processor count, CIRCUITPY_* defaults
#include "mpconfigboard.h"

// ── Display ──
// 480×360 gives 80×30 terminal at 6×12 font, scale=1
#define WASM_DISPLAY_WIDTH  480
#define WASM_DISPLAY_HEIGHT 360

#define CIRCUITPY_DISPLAYIO         (1)
#define CIRCUITPY_FRAMEBUFFERIO     (1)
#define CIRCUITPY_TERMINALIO        (1)
#define CIRCUITPY_FONTIO            (1)
#define CIRCUITPY_REPL_LOGO         (1)

// ── Common-HAL hardware modules ──
#define CIRCUITPY_DIGITALIO         (1)
#define CIRCUITPY_ANALOGIO          (1)
#define CIRCUITPY_PWMIO             (1)
#define CIRCUITPY_NEOPIXEL_WRITE    (1)
#define CIRCUITPY_MICROCONTROLLER   (1)

// ── Bus I/O (I2C, SPI, UART) ──
// Enables `import busio` and the board.I2C() / board.SPI() / board.UART()
// convenience constructors.  Pin assignments match definition.json.
#undef CIRCUITPY_BUSIO
#define CIRCUITPY_BUSIO             (1)

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
// JS pushes keyboard events via /sys/events (semihosting protocol).
// pyexec_event_repl_process_char() is a pure state machine — no blocking.
// When user hits Enter, execution can yield via MP_VM_RETURN_YIELD.
#define MICROPY_REPL_EVENT_DRIVEN   (1)

// ── Status bar (rendered in terminal top row) ──
#define CIRCUITPY_STATUS_BAR        (1)

// ── Display utilities (pure shared-module, no common-hal) ──
#undef CIRCUITPY_VECTORIO
#define CIRCUITPY_VECTORIO          (1)
#undef CIRCUITPY_BITMAPTOOLS
#define CIRCUITPY_BITMAPTOOLS       (1)

// ── Pure software modules (no common-hal needed) ──
// These are commonly available on popular boards and essential
// for running Adafruit Learn Guide examples.

// rainbowio — colorwheel() used in nearly every NeoPixel guide
#define CIRCUITPY_RAINBOWIO         (1)

// keypad — deferred (needs supervisor_acquire_lock, port_malloc_zero stubs)

// touchio — software capacitive touch (uses analogio internally)
#define CIRCUITPY_TOUCHIO           (1)
#define CIRCUITPY_TOUCHIO_USE_NATIVE (0)
// gifio — deferred (verify dependencies first)

// ── jsffi (JavaScript FFI via WASM imports) ──
// Enables `import jsffi` — Python can access JS objects via JsProxy.
#undef MICROPY_PY_JSFFI
#define MICROPY_PY_JSFFI            (1)

// ── Board ──
// CIRCUITPY_BOARD set via -D in mpconfigvariant.mk
