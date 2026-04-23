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

/* ------------------------------------------------------------------ */
/* Pin metadata: role, flags, category                                 */
/*                                                                     */
/* Per-pin metadata in port_mem.pin_meta[64].  Self-describing state   */
/* visible to both C and JS via linear memory.                         */
/* ------------------------------------------------------------------ */

/* Role: what peripheral Python is using this pin as RIGHT NOW.
 * Set by common-hal construct(), cleared by deinit()/reset. */
#define HAL_ROLE_UNCLAIMED   0x00
#define HAL_ROLE_DIGITAL_IN  0x01
#define HAL_ROLE_DIGITAL_OUT 0x02
#define HAL_ROLE_ADC         0x03
#define HAL_ROLE_DAC         0x04
#define HAL_ROLE_PWM         0x05
#define HAL_ROLE_NEOPIXEL    0x06
#define HAL_ROLE_I2C         0x07
#define HAL_ROLE_SPI         0x08
#define HAL_ROLE_UART        0x09

/* Category: what the board designed this pin for.
 * Set once at init from the static board table.  Survives reset.
 * Ordered by visual priority — highest value wins when multiple
 * board names map to the same GPIO (e.g., D13 + LED + SCK). */
#define HAL_CAT_NONE         0x00
#define HAL_CAT_DIGITAL      0x01   /* D0-D13 */
#define HAL_CAT_ANALOG       0x02   /* A0-A5 */
#define HAL_CAT_BUS_UART     0x03   /* TX, RX */
#define HAL_CAT_BUS_SPI      0x04   /* MOSI, MISO, SCK */
#define HAL_CAT_BUS_I2C      0x05   /* SDA, SCL */
#define HAL_CAT_NEOPIXEL     0x06   /* NEOPIXEL */
#define HAL_CAT_LED          0x07   /* LED */
#define HAL_CAT_BUTTON       0x08   /* BUTTON_A, BUTTON_B */

/* Flags: report/ack bitmask.
 * Each side sets its own "wrote" flag; clears the other's after reading. */
#define HAL_FLAG_JS_WROTE    0x01  /* JS wrote a new value */
#define HAL_FLAG_C_WROTE     0x02  /* C wrote a new value */
#define HAL_FLAG_C_READ      0x04  /* C has read the current value */
#define HAL_FLAG_LATCHED     0x08  /* Port latched an input value (like IRQ capture) */

/* API */
void    hal_set_role(uint8_t pin, uint8_t role);
void    hal_clear_role(uint8_t pin);
void    hal_set_flag(uint8_t pin, uint8_t flag);
void    hal_clear_flag(uint8_t pin, uint8_t flag);
uint8_t hal_get_flags(uint8_t pin);
void    hal_set_category(uint8_t pin, uint8_t category);

/* Called from board_pins.c to populate categories at init. */
void    hal_init_pin_categories(void);
