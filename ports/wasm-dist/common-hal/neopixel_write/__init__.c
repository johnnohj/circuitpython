/*
 * neopixel_write — Virtual NeoPixel via /hal/neopixel fd endpoint.
 *
 * Writes pixel data for a pin to the /hal/neopixel fd.
 * Format: 4-byte header + pixel data per write.
 *   [0]   pin number  (uint8)
 *   [1]   enabled     (uint8)
 *   [2-3] num_bytes   (uint16 LE)
 *   [4+]  pixel data  (GRB or GRBW bytes)
 *
 * JS reads the entire fd content to get all neopixel state.
 * Each write replaces the data for that pin.
 */

#include "shared-bindings/neopixel_write/__init__.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "supervisor/hal.h"
#include "py/runtime.h"

#include <string.h>
#include <unistd.h>

#define NEOPIXEL_HEADER_SIZE 4
#define MAX_PIXEL_BYTES (256 * 4)

void common_hal_neopixel_write(const digitalio_digitalinout_obj_t *digitalinout,
    uint8_t *pixels, uint32_t numBytes) {
    if (digitalinout->pin == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pin is deinit"));
        return;
    }

    int fd = hal_neopixel_fd();
    if (fd < 0) return;

    uint8_t pin = digitalinout->pin->number;
    if (numBytes > MAX_PIXEL_BYTES) {
        numBytes = MAX_PIXEL_BYTES;
    }

    /* Write header + pixel data at pin's offset.
     * Each pin gets a fixed-size region to keep it seekable. */
    uint32_t offset = pin * (NEOPIXEL_HEADER_SIZE + MAX_PIXEL_BYTES);
    uint8_t header[NEOPIXEL_HEADER_SIZE];
    header[0] = pin;
    header[1] = 1; /* enabled */
    header[2] = numBytes & 0xFF;
    header[3] = (numBytes >> 8) & 0xFF;

    lseek(fd, offset, SEEK_SET);
    write(fd, header, NEOPIXEL_HEADER_SIZE);
    write(fd, pixels, numBytes);
}
