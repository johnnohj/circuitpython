/*
 * supervisor_stubs.c — Stubs for supervisor functions referenced
 * by shared-module code but not needed in the WASI port.
 */

#include <stdbool.h>
#include <stdint.h>

#include "py/obj.h"

// Referenced by shared-module/displayio/__init__.c (display refresh loop)
bool autoreload_ready(void) {
    return false;
}

// Referenced by shared-bindings/displayio/__init__.c
// board_serial_write_substring is called at end of serial_write_substring
void board_serial_write_substring(const char *text, uint32_t length) {
    (void)text;
    (void)length;
}

// Referenced by supervisor/shared/display.c via status_leds.h
void toggle_rx_led(void) {}
void toggle_tx_led(void) {}

// OnDiskBitmap — references exist in TileGrid and displayio/__init__
// but we don't include OnDiskBitmap.c (needs FatFS). Stub the type
// so runtime type checks work (they'll never match).
#include "shared-module/displayio/OnDiskBitmap.h"
const mp_obj_type_t displayio_ondiskbitmap_type = {
    .base = { .type = &mp_type_type },
    .name = MP_QSTR_OnDiskBitmap,
};

uint32_t common_hal_displayio_ondiskbitmap_get_pixel(displayio_ondiskbitmap_t *self,
    int16_t x, int16_t y) {
    (void)self; (void)x; (void)y;
    return 0;
}
