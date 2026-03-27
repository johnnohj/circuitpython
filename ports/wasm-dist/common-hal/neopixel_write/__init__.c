/*
 * neopixel_write — Virtual NeoPixel for WASI port.
 *
 * Copies pixel data to a per-pin state array. The worker flushes
 * this to /hw/neopixel/data for JS visualization.
 */

#include "shared-bindings/neopixel_write/__init__.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "hw_state.h"
#include "py/runtime.h"
#include <string.h>

#define MAX_LEDS_PER_PIN 256

typedef struct {
    uint8_t pixels[MAX_LEDS_PER_PIN * 4];  /* RGBW max */
    uint32_t num_bytes;
    bool enabled;
} neopixel_pin_state_t;

neopixel_pin_state_t neopixel_state[64];

void neopixel_reset_state(void) {
    for (int i = 0; i < 64; i++) {
        memset(neopixel_state[i].pixels, 0, MAX_LEDS_PER_PIN * 4);
        neopixel_state[i].num_bytes = 0;
        neopixel_state[i].enabled = false;
    }
}

void common_hal_neopixel_write(const digitalio_digitalinout_obj_t *digitalinout,
    uint8_t *pixels, uint32_t numBytes) {
    if (digitalinout->pin == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("Pin is deinit"));
        return;
    }
    uint8_t n = digitalinout->pin->number;
    if (numBytes > MAX_LEDS_PER_PIN * 4) {
        numBytes = MAX_LEDS_PER_PIN * 4;
    }
    memcpy(neopixel_state[n].pixels, pixels, numBytes);
    neopixel_state[n].num_bytes = numBytes;
    neopixel_state[n].enabled = true;
    hw_neopixel_dirty = true;
}
