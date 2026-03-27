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

// ── Board ──
// CIRCUITPY_BOARD set via -D in mpconfigvariant.mk
