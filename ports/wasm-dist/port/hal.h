// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/hal.h by CircuitPython contributors
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/hal.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/hal.h — Hardware abstraction: GPIO, analog, dirty tracking.
//
// All hardware state lives in port_mem (MEMFS-in-linear-memory).
// C accesses via pointer, JS via memfs — same bytes.  No FD-based I/O.
//
// Pin claim/release follows CircuitPython's common_hal pattern:
// construct() claims, deinit()/reset releases.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, peripheral simulation)
//   design/behavior/01-hardware-init.md     (GPIO layout, 32 pins, categories)

#ifndef PORT_HAL_H
#define PORT_HAL_H

#include <stdint.h>
#include <stdbool.h>

// ── Lifecycle ──

// Initialize HAL (zero gpio/analog state, clear dirty flags).
void hal_init(void);

// Per-frame step: scan dirty flags, latch input values, clear dirty.
// Called at the top of the frame, before VM work.
void hal_step(void);

// ── Pin claim/release ──
// Mirrors CircuitPython's common_hal construct/deinit pattern.

// Claim a pin for a role.  Returns true if successful.
// Fails if pin is already claimed by a different role.
// Idempotent: claiming for the same role succeeds.
bool hal_claim_pin(uint8_t pin, uint8_t role);

// Release a pin — resets role to UNCLAIMED, clears state.
void hal_release_pin(uint8_t pin);

// Release all claimed pins (soft reset).
void hal_release_all(void);

// ── Pin query ──

bool hal_pin_is_claimed(uint8_t pin);
uint8_t hal_pin_role(uint8_t pin);
uint32_t hal_claimed_count(void);

// ── Pin read/write ──
// Direct memory access — no FD calls.

// Write a pin value from C side (sets C_WROTE flag, notifies JS).
void hal_write_pin(uint8_t pin, uint8_t value);

// Read a pin value.  Returns latched value for inputs, current for outputs.
uint8_t hal_read_pin(uint8_t pin);

// ── Pin metadata ──
// Stored in GPIO slot bytes [4-7].  Direct memory, not FD read-modify-write.

// Direction values
#define HAL_DIR_INPUT             0
#define HAL_DIR_OUTPUT            1
#define HAL_DIR_OUTPUT_OPEN_DRAIN 2

// Enabled tri-state values
#define HAL_ENABLED_NEVER_RESET  (-1)
#define HAL_ENABLED_NO            0
#define HAL_ENABLED_YES           1

// Category: what the board designed this pin for.
// Set once at init from the board pin table.  Survives reset.
// Ordered by visual priority — highest value wins when multiple
// board names map to the same GPIO (e.g., D13 + LED + SCK).
#define HAL_CAT_NONE         0x00
#define HAL_CAT_DIGITAL      0x01   // D0-D13
#define HAL_CAT_ANALOG       0x02   // A0-A5
#define HAL_CAT_BUS_UART     0x03   // TX, RX
#define HAL_CAT_BUS_SPI      0x04   // MOSI, MISO, SCK
#define HAL_CAT_BUS_I2C      0x05   // SDA, SCL
#define HAL_CAT_NEOPIXEL     0x06   // NEOPIXEL
#define HAL_CAT_LED          0x07   // LED
#define HAL_CAT_BUTTON       0x08   // BUTTON_A, BUTTON_B

// Metadata accessors — direct memory into port_mem GPIO slot
void    hal_set_role(uint8_t pin, uint8_t role);
void    hal_clear_role(uint8_t pin);
void    hal_set_direction(uint8_t pin, uint8_t direction);
uint8_t hal_get_direction(uint8_t pin);
void    hal_set_pull(uint8_t pin, uint8_t pull);
uint8_t hal_get_pull(uint8_t pin);
void    hal_set_flag(uint8_t pin, uint8_t flag);
void    hal_clear_flag(uint8_t pin, uint8_t flag);
uint8_t hal_get_flags(uint8_t pin);
void    hal_set_category(uint8_t pin, uint8_t category);
uint8_t hal_get_category(uint8_t pin);

// Called from board pins.c to populate categories at init.
// Weak default: no board table → categories stay zeroed.
void hal_init_pin_categories(void);

// ── Dirty flag API ──
// Dirty bits in port_mem track which pins changed since last hal_step().
// JS calls the WASM exports below; common-hal writes call hal_mark_*.

void hal_mark_gpio_dirty(uint8_t pin);
void hal_mark_analog_dirty(uint8_t pin);

bool hal_gpio_changed(uint8_t pin);
bool hal_analog_changed(uint8_t pin);

uint32_t hal_gpio_drain_dirty(void);
uint32_t hal_analog_drain_dirty(void);

#endif // PORT_HAL_H
