/*
 * hw_state.c — Hardware state dirty flags.
 *
 * The worker's common-hal modules set these flags when state arrays
 * (gpio_state[], analog_state[], pwm_state[]) are mutated.
 * worker_u2if.c checks them to build diff packets for the main thread.
 */

#include "hw_state.h"

bool hw_gpio_dirty    = false;
bool hw_analog_dirty  = false;
bool hw_pwm_dirty     = false;
bool hw_neopixel_dirty = false;
