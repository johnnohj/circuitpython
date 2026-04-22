/*
 * supervisor/hal.h — WASM hardware abstraction layer.
 *
 * Hardware state lives at /hal/ WASI fd endpoints (in MEMFS).
 * Common-hal modules use these fds to read/write peripheral state.
 * wasi-memfs.js intercepts the WASI calls on the JS side.
 *
 * Dirty flags in port_mem track which pins changed externally (JS
 * wrote to a /hal/ endpoint).  hal_step() reads and clears them,
 * alerting the supervisor via background callbacks when needed.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Lifecycle */
void hal_init(void);
void hal_step(void);
void hal_export_dirty(void);

/* fd accessors — returns open fd or -1 if not initialized */
int hal_gpio_fd(void);
int hal_analog_fd(void);
int hal_pwm_fd(void);
int hal_neopixel_fd(void);
int hal_serial_rx_fd(void);
int hal_serial_tx_fd(void);

/* ------------------------------------------------------------------ */
/* Dirty flag API                                                      */
/*                                                                     */
/* JS sets dirty bits in port_mem when it writes to /hal/ endpoints    */
/* (via updateHardwareState).  C code queries/clears them here.        */
/* ------------------------------------------------------------------ */

/* Set a dirty bit for a pin on a specific peripheral.
 * Called from JS via exported WASM function (hal_mark_dirty). */
void hal_mark_gpio_dirty(uint8_t pin);
void hal_mark_analog_dirty(uint8_t pin);
void hal_mark_pwm_dirty(uint8_t pin);
void hal_mark_neopixel_dirty(void);

/* Check if a pin changed since last hal_step().
 * Common-hal modules call these for edge detection. */
bool hal_gpio_changed(uint8_t pin);
bool hal_analog_changed(uint8_t pin);

/* Get and clear all dirty bits for a peripheral (used by hal_step). */
uint64_t hal_gpio_drain_dirty(void);
uint64_t hal_analog_drain_dirty(void);
