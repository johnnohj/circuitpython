/*
 * hw_state.h — Hardware state dirty flags.
 *
 * Set by common-hal code when state arrays are mutated.
 * Checked by worker_u2if.c to build diff packets.
 */

#pragma once

#include <stdbool.h>

extern bool hw_gpio_dirty;
extern bool hw_analog_dirty;
extern bool hw_pwm_dirty;
extern bool hw_neopixel_dirty;
