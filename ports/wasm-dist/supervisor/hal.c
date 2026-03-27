/*
 * supervisor/hal.c — WASM hardware simulation driver.
 *
 * This concept does not exist in real CircuitPython because real hardware
 * runs itself (DMA, interrupts, clocks).  On WASM, nothing happens unless
 * we make it happen.  The HAL step runs once per frame (cp_step), before
 * any Python bytecodes execute, to update simulated hardware state so
 * Python sees fresh data.
 *
 * hal_step():
 *   Called at the top of cp_step().  Polls /hal/ WASI fd endpoints for
 *   changes made by JS since the last frame.  Updates internal state
 *   caches that common-hal modules read.
 *
 * hal_export_dirty():
 *   Called at the bottom of cp_step().  Flushes any hardware state changes
 *   that Python made (via common-hal) back to /hal/ endpoints so JS can
 *   read them.
 *
 * Initially, most sub-functions are no-ops.  As common-hal modules are
 * added (digitalio, analogio, displayio), each registers its poll and
 * export functions here.
 */

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* HAL step — drive simulated hardware before Python runs              */
/* ------------------------------------------------------------------ */

void hal_step(void) {
    /* Serial: nothing to do here — serial_read() checks rx buffer
     * inline.  Future: poll /hal/serial/rx fd for new bytes. */

    /* GPIO: read /hal/gpio fd, update pin state cache.
     * TODO: implement when common-hal/digitalio is added. */

    /* Timers: advance any simulated timer/counter peripherals.
     * TODO: implement when common-hal/pulseio or countio is added. */

    /* Display: check if framebuffer was externally modified.
     * TODO: implement when displayio is wired. */

    /* Sensors: update simulated ADC/I2C/SPI values.
     * TODO: implement when common-hal/analogio, busio is added. */
}

/* ------------------------------------------------------------------ */
/* HAL export — flush Python's hw changes to /hal/ endpoints           */
/* ------------------------------------------------------------------ */

void hal_export_dirty(void) {
    /* GPIO: write changed pin states to /hal/gpio fd.
     * TODO: implement when common-hal/digitalio is added. */

    /* Display: export framebuffer pointer/dirty flag for JS.
     * TODO: implement when displayio is wired. */
}
