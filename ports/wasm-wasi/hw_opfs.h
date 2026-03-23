/*
 * hw_opfs.h — OPFS read/write-through for hardware state arrays.
 *
 * The worker's C common-hal modules maintain in-memory state arrays
 * (gpio_state[], analog_state[], etc.).  This module syncs them to
 * OPFS endpoint files so the reactor's Python shims and the JS host
 * can see hardware state changes.
 *
 * File layout: raw struct arrays written with POSIX write().
 * Since both the reactor (Python struct.pack) and the worker (C)
 * run on wasm32-le, the binary layout is identical.
 *
 * Usage (called from worker poll loop):
 *   hw_opfs_read_all()   — OPFS → state arrays (pick up reactor writes)
 *   hw_opfs_flush_all()  — state arrays → OPFS (publish worker changes)
 *
 * Dirty tracking: each module sets a flag when state is mutated.
 * hw_opfs_flush_all() only writes files whose flag is set.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/* ---- Endpoint paths ---- */
#define HW_GPIO_STATE_PATH     "/hw/gpio/state"
#define HW_ANALOG_STATE_PATH   "/hw/analog/state"
#define HW_PWM_STATE_PATH      "/hw/pwm/state"
#define HW_NEOPIXEL_DATA_PATH  "/hw/neopixel/data"

/* ---- Init (create directories + seed files) ---- */
void hw_opfs_init(void);

/* ---- Bulk operations (called from worker poll loop) ---- */

/* Read all endpoint files into state arrays.
 * Skips files that haven't changed (stat mtime check). */
void hw_opfs_read_all(void);

/* Write dirty state arrays to endpoint files.
 * Clears dirty flags after writing. */
void hw_opfs_flush_all(void);

/* ---- Per-module dirty flags ---- */
/* Set by common-hal code on state mutation. */
extern bool hw_opfs_gpio_dirty;
extern bool hw_opfs_analog_dirty;
extern bool hw_opfs_pwm_dirty;
extern bool hw_opfs_neopixel_dirty;

/* ---- Per-module read/write (for targeted sync) ---- */
void hw_opfs_gpio_read(void);
void hw_opfs_gpio_write(void);
void hw_opfs_analog_read(void);
void hw_opfs_analog_write(void);
void hw_opfs_pwm_read(void);
void hw_opfs_pwm_write(void);
void hw_opfs_neopixel_write(void);
