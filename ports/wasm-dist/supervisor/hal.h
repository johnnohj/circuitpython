/*
 * supervisor/hal.h — WASM hardware abstraction layer.
 *
 * Hardware state lives at /hal/ WASI fd endpoints.
 * Common-hal modules use these fds to read/write peripheral state.
 * wasi-memfs.js intercepts the WASI calls on the JS side.
 */

#pragma once

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
