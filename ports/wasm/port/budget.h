// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/budget.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/budget.h — Frame budget tracking.
//
// Soft deadline (default 8ms): work should try to finish.
// Firm deadline (default 10ms): must stop, return to JS.
// The 2ms buffer allows the current VM operation to complete without
// surprising the frame deadline.
//
// Uses clock_gettime(CLOCK_MONOTONIC) for sub-ms precision.
// On WASM, this resolves to the WASI clock_time_get import
// which returns performance.now() from JS.
//
// Design refs:
//   design/behavior/06-runtime-environments.md  (frame budget model)
//   design/wasm-layer.md                        (wasm layer timing)

#ifndef PORT_BUDGET_H
#define PORT_BUDGET_H

#include <stdint.h>
#include <stdbool.h>

// Default deadlines
#define BUDGET_SOFT_US   8000   // 8ms — start wrapping up
#define BUDGET_FIRM_US  10000   // 10ms — must return to JS
#define BUDGET_SOFT_MS   (BUDGET_SOFT_US / 1000)  // 8ms for vm_yield

// Start tracking a frame's budget.
// Call at the top of the frame function.
void budget_frame_start(void);

// Get elapsed microseconds since budget_frame_start()
uint32_t budget_elapsed_us(void);

// Check if we've passed the soft deadline
bool budget_soft_expired(void);

// Check if we've passed the firm deadline
bool budget_firm_expired(void);

// Set custom deadlines (0 = use default)
void budget_set_deadlines(uint32_t soft_us, uint32_t firm_us);

// Get the current deadlines
uint32_t budget_get_soft_us(void);
uint32_t budget_get_firm_us(void);

#endif // PORT_BUDGET_H
