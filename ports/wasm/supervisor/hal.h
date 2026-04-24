/*
 * supervisor/hal.h — WASM hardware abstraction layer.
 *
 * All hardware state lives in MEMFS at /hal/ WASI fd endpoints.
 * MEMFS is the substrate — off-stack persistent state that survives
 * C stack unwinding on VM yield/suspend.  Common-hal modules read/write
 * these fds; wasi-memfs.js manages the backing store on the JS side.
 *
 * JS→C input changes are routed through the semihosting event ring
 * (SH_EVT_HW_CHANGE).  The event handler writes to MEMFS, sets flags,
 * and latches input values — preserving individual events (button
 * press + release = two events, not one collapsed dirty flag).
 *
 * Dirty flags in port_mem track which pins changed.  hal_step() checks
 * them and wakes the supervisor via background callbacks when needed.
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
/* GPIO slot layout (12 bytes per pin in /hal/gpio MEMFS endpoint)     */
/*                                                                     */
/* All pin state — electrical AND metadata — lives in one MEMFS slot.  */
/* One read() gets everything; one write() updates atomically.         */
/*                                                                     */
/* Compressed from the original 8-byte electrical + 4-byte pin_meta:   */
/*   - enabled absorbs never_reset as a tri-state (-1/0/1)            */
/*   - direction absorbs open_drain (0=in, 1=out, 2=out_open_drain)   */
/*   - role, flags, category, latched folded in from pin_meta          */
/*                                                                     */
/*   [0]  enabled    int8: -1=never_reset, 0=disabled, 1=enabled      */
/*   [1]  direction  uint8: 0=input, 1=output, 2=output_open_drain   */
/*   [2]  value      uint8: 0/1                                       */
/*   [3]  pull       uint8: 0=none, 1=up, 2=down                     */
/*   [4]  role       uint8: HAL_ROLE_*                                */
/*   [5]  flags      uint8: HAL_FLAG_*                                */
/*   [6]  category   uint8: HAL_CAT_*                                 */
/*   [7]  latched    uint8: captured input value (IRQ-like)           */
/*   [8-11] reserved                                                   */
/* ------------------------------------------------------------------ */

#define HAL_GPIO_SLOT_SIZE   12

/* Byte offsets within a GPIO slot */
#define HAL_GPIO_OFF_ENABLED    0
#define HAL_GPIO_OFF_DIRECTION  1
#define HAL_GPIO_OFF_VALUE      2
#define HAL_GPIO_OFF_PULL       3
#define HAL_GPIO_OFF_ROLE       4
#define HAL_GPIO_OFF_FLAGS      5
#define HAL_GPIO_OFF_CATEGORY   6
#define HAL_GPIO_OFF_LATCHED    7

/* Enabled tri-state values */
#define HAL_ENABLED_NEVER_RESET  (-1)
#define HAL_ENABLED_NO            0
#define HAL_ENABLED_YES           1

/* Direction values (absorbs open_drain) */
#define HAL_DIR_INPUT             0
#define HAL_DIR_OUTPUT            1
#define HAL_DIR_OUTPUT_OPEN_DRAIN 2

/* ------------------------------------------------------------------ */
/* Dirty flag API                                                      */
/*                                                                     */
/* Dirty bits in port_mem track which pins changed.  Set by the        */
/* SH_EVT_HW_CHANGE handler (for JS input) or by common-hal writes.   */
/* ------------------------------------------------------------------ */

void hal_mark_gpio_dirty(uint8_t pin);
void hal_mark_analog_dirty(uint8_t pin);
void hal_mark_pwm_dirty(uint8_t pin);
void hal_mark_neopixel_dirty(void);

bool hal_gpio_changed(uint8_t pin);
bool hal_analog_changed(uint8_t pin);

uint64_t hal_gpio_drain_dirty(void);
uint64_t hal_analog_drain_dirty(void);

/* ------------------------------------------------------------------ */
/* Pin metadata — stored in MEMFS GPIO slot bytes [6-9]                */
/*                                                                     */
/* All metadata lives in the MEMFS slot alongside electrical state.    */
/* The hal_set/get API does read-modify-write on the MEMFS fd.         */
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

/* API — read-modify-write through MEMFS fd */
void    hal_set_role(uint8_t pin, uint8_t role);
void    hal_clear_role(uint8_t pin);
void    hal_set_flag(uint8_t pin, uint8_t flag);
void    hal_clear_flag(uint8_t pin, uint8_t flag);
uint8_t hal_get_flags(uint8_t pin);
void    hal_set_category(uint8_t pin, uint8_t category);
uint8_t hal_get_category(uint8_t pin);

/* Called from board_pins.c to populate categories at init. */
void    hal_init_pin_categories(void);
