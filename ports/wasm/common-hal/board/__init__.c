/*
 * board/__init__.c — Board bus instances for WASM port.
 *
 * Default I2C/SPI/UART creation is handled by shared-module/board/__init__.c
 * when CIRCUITPY_BOARD_I2C, CIRCUITPY_BOARD_SPI, CIRCUITPY_BOARD_UART are
 * defined (see variants/browser/mpconfigvariant.h).
 *
 * This file is intentionally minimal — it exists only because the build
 * system expects a common-hal/board/__init__.c to exist.
 */

#include "shared-bindings/board/__init__.h"
