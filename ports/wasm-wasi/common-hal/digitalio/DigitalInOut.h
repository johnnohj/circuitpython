/*
 * DigitalInOut.h — Virtual GPIO for WASI port.
 *
 * State lives in a flat array; OPFS mirrors it for cross-instance access.
 * Adapted from ports/wasm/common-hal/digitalio/DigitalInOut.h
 * Stripped: JsProxy, EM_JS.
 */
#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool value;
    uint8_t direction;   /* 0=input, 1=output */
    uint8_t pull;        /* 0=none, 1=up, 2=down */
    bool open_drain;
    bool enabled;
    bool never_reset;
} gpio_pin_state_t;

extern gpio_pin_state_t gpio_state[64];

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} digitalio_digitalinout_obj_t;

void digitalio_reset_gpio_state(void);
