/*
 * chassis/budget.h — Frame budget tracking.
 *
 * Soft deadline (default 8ms): work should try to finish.
 * Firm deadline (default 10ms): must stop, return to JS.
 *
 * Uses clock_gettime(CLOCK_MONOTONIC) for sub-ms precision.
 * On WASM, this resolves to the WASI clock_time_get import
 * which returns performance.now() from JS.
 */

#ifndef CHASSIS_BUDGET_H
#define CHASSIS_BUDGET_H

#include <stdint.h>
#include <stdbool.h>

/* Default deadlines in microseconds */
#define BUDGET_SOFT_US   8000   /* 8ms — start wrapping up */
#define BUDGET_FIRM_US  10000   /* 10ms — must return to JS */

/* Start tracking a frame's budget.
 * Call at the top of chassis_frame(). */
void budget_frame_start(void);

/* Get elapsed microseconds since budget_frame_start() */
uint32_t budget_elapsed_us(void);

/* Check if we've passed the soft deadline */
bool budget_soft_expired(void);

/* Check if we've passed the firm deadline */
bool budget_firm_expired(void);

/* Set custom deadlines (0 = use default) */
void budget_set_deadlines(uint32_t soft_us, uint32_t firm_us);

/* Get the current deadlines */
uint32_t budget_get_soft_us(void);
uint32_t budget_get_firm_us(void);

#endif /* CHASSIS_BUDGET_H */
