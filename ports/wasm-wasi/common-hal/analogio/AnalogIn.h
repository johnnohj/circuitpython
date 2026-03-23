#pragma once
#include "common-hal/microcontroller/Pin.h"
#include "py/obj.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t value;
    bool is_output;
    bool enabled;
} analog_pin_state_t;

extern analog_pin_state_t analog_state[64];

typedef struct {
    mp_obj_base_t base;
    const mcu_pin_obj_t *pin;
} analogio_analogin_obj_t;

void analogio_reset_analog_state(void);
