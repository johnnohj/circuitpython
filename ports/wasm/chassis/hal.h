/*
 * chassis/hal.h — HAL layer with claim/release and budget-aware stepping.
 *
 * The HAL knows which pins are claimed and by whom.  It only processes
 * changes on claimed pins (unclaimed pin changes are noted but don't
 * wake anything).  Claim/release are explicit — like CircuitPython's
 * common_hal_*_construct/deinit pattern.
 */

#ifndef CHASSIS_HAL_H
#define CHASSIS_HAL_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize HAL (zero state, called from chassis_init) */
void hal_init(void);

/* Per-frame step: scan dirty flags, latch values, track claims */
void hal_step(void);

/* Claim a pin for a role.  Returns true if successful.
 * Fails if pin is already claimed by a different role. */
bool hal_claim_pin(uint8_t pin, uint8_t role);

/* Release a pin — resets role to UNCLAIMED, clears flags */
void hal_release_pin(uint8_t pin);

/* Release all claimed pins (soft reset) */
void hal_release_all(void);

/* Query: is pin claimed? */
bool hal_pin_is_claimed(uint8_t pin);

/* Query: what role is pin claimed as? */
uint8_t hal_pin_role(uint8_t pin);

/* Write a pin value from C side (sets C_WROTE flag) */
void hal_write_pin(uint8_t pin, uint8_t value);

/* Read a pin value (returns latched value for inputs) */
uint8_t hal_read_pin(uint8_t pin);

/* Get count of currently claimed pins */
uint32_t hal_claimed_count(void);

#endif /* CHASSIS_HAL_H */
