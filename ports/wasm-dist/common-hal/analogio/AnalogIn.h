/*
 * AnalogIn.h — Virtual ADC via /hal/analog fd endpoint.
 */
#pragma once
#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} analogio_analogin_obj_t;
