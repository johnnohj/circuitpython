/*
 * DigitalInOut.h — Virtual GPIO via /hal/gpio fd endpoint.
 *
 * Pin state lives at the /hal/gpio WASI fd, not in a C array.
 * Each pin occupies 8 bytes at offset pin_number * 8.
 */
#pragma once

#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} digitalio_digitalinout_obj_t;
