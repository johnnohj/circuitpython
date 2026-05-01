// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/hal.c by CircuitPython contributors
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/hal.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/hal.c — HAL implementation: claim/release, dirty scan, latch,
// metadata, WASM exports.
//
// Merges the chassis direct-memory access pattern with the supervisor's
// category/role/flag system.  All access is via pointers into port_mem —
// no FD-based I/O (the old supervisor pattern used lseek/read/write on
// MEMFS fds; that's replaced by direct memory here).
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, peripheral simulation)
//   design/behavior/01-hardware-init.md     (GPIO layout, 32 pins)

#include "port/hal.h"
#include "port/port_memory.h"
#include "port/constants.h"
#include "port/ffi_imports.h"
#include <string.h>

// ── Init / Reset ──

void hal_init(void) {
    memset(port_mem.hal_gpio, 0, sizeof(port_mem.hal_gpio));
    memset(port_mem.hal_analog, 0, sizeof(port_mem.hal_analog));
    port_mem.hal_gpio_dirty = 0;
    port_mem.hal_analog_dirty = 0;
    port_mem.hal_change_count = 0;

    hal_init_pin_categories();
}

// ── Claim / Release ──

bool hal_claim_pin(uint8_t pin, uint8_t role) {
    if (pin >= GPIO_MAX_PINS || role == ROLE_UNCLAIMED) return false;

    uint8_t *slot = gpio_slot(pin);
    uint8_t current_role = slot[GPIO_ROLE];

    // Already claimed by same role = OK (idempotent)
    if (current_role == role) return true;

    // Claimed by different role = fail
    if (current_role != ROLE_UNCLAIMED) return false;

    slot[GPIO_ROLE] = role;
    slot[GPIO_ENABLED] = HAL_ENABLED_YES;
    slot[GPIO_FLAGS] |= GF_C_WROTE;

    // Set direction based on role.
    // Per design: C owns input/output directionality and high/True/low/False logic.
    if (role == ROLE_DIGITAL_OUT) {
        slot[GPIO_DIRECTION] = HAL_DIR_OUTPUT;
    } else if (role == ROLE_DIGITAL_IN || role == ROLE_ADC) {
        slot[GPIO_DIRECTION] = HAL_DIR_INPUT;
    }

    return true;
}

void hal_release_pin(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return;

    uint8_t *slot = gpio_slot(pin);
    uint8_t category = slot[GPIO_CATEGORY];  // preserve category across reset

    slot[GPIO_ROLE] = ROLE_UNCLAIMED;
    slot[GPIO_ENABLED] = HAL_ENABLED_NO;
    slot[GPIO_FLAGS] = GF_C_WROTE;
    slot[GPIO_VALUE] = 0;
    slot[GPIO_PULL] = 0;
    slot[GPIO_DIRECTION] = HAL_DIR_INPUT;
    slot[GPIO_LATCHED] = 0;
    slot[GPIO_CATEGORY] = category;  // category survives reset
}

void hal_release_all(void) {
    for (int i = 0; i < GPIO_MAX_PINS; i++) {
        if (gpio_slot(i)[GPIO_ROLE] != ROLE_UNCLAIMED) {
            hal_release_pin(i);
        }
    }
}

// ── Query ──

bool hal_pin_is_claimed(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return false;
    return gpio_slot(pin)[GPIO_ROLE] != ROLE_UNCLAIMED;
}

uint8_t hal_pin_role(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return ROLE_UNCLAIMED;
    return gpio_slot(pin)[GPIO_ROLE];
}

uint32_t hal_claimed_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < GPIO_MAX_PINS; i++) {
        if (gpio_slot(i)[GPIO_ROLE] != ROLE_UNCLAIMED) {
            count++;
        }
    }
    return count;
}

// ── Read / Write ──

void hal_write_pin(uint8_t pin, uint8_t value) {
    if (pin >= GPIO_MAX_PINS) return;
    uint8_t *slot = gpio_slot(pin);
    slot[GPIO_VALUE] = value ? 1 : 0;
    slot[GPIO_FLAGS] = (slot[GPIO_FLAGS] & ~GF_JS_WROTE) | GF_C_WROTE;
    ffi_notify(NOTIFY_PIN_CHANGED, pin, value ? 1 : 0, 0);
}

uint8_t hal_read_pin(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return 0;
    uint8_t *slot = gpio_slot(pin);
    // For inputs, return latched value; for outputs, return current value
    if (slot[GPIO_DIRECTION] == HAL_DIR_INPUT) {
        return slot[GPIO_LATCHED];
    }
    return slot[GPIO_VALUE];
}

// ── Pin metadata — direct memory access ──

void hal_set_role(uint8_t pin, uint8_t role) {
    if (pin >= GPIO_MAX_PINS) return;
    gpio_slot(pin)[GPIO_ROLE] = role;
}

void hal_clear_role(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return;
    gpio_slot(pin)[GPIO_ROLE] = ROLE_UNCLAIMED;
    gpio_slot(pin)[GPIO_FLAGS] = 0;
}

void hal_set_direction(uint8_t pin, uint8_t direction) {
    if (pin >= GPIO_MAX_PINS) return;
    gpio_slot(pin)[GPIO_DIRECTION] = direction;
}

uint8_t hal_get_direction(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return HAL_DIR_INPUT;
    return gpio_slot(pin)[GPIO_DIRECTION];
}

void hal_set_pull(uint8_t pin, uint8_t pull) {
    if (pin >= GPIO_MAX_PINS) return;
    gpio_slot(pin)[GPIO_PULL] = pull;
}

uint8_t hal_get_pull(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return 0;
    return gpio_slot(pin)[GPIO_PULL];
}

void hal_set_flag(uint8_t pin, uint8_t flag) {
    if (pin >= GPIO_MAX_PINS) return;
    gpio_slot(pin)[GPIO_FLAGS] |= flag;
}

void hal_clear_flag(uint8_t pin, uint8_t flag) {
    if (pin >= GPIO_MAX_PINS) return;
    gpio_slot(pin)[GPIO_FLAGS] &= ~flag;
}

uint8_t hal_get_flags(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return 0;
    return gpio_slot(pin)[GPIO_FLAGS];
}

void hal_set_category(uint8_t pin, uint8_t category) {
    if (pin >= GPIO_MAX_PINS) return;
    gpio_slot(pin)[GPIO_CATEGORY] = category;
}

uint8_t hal_get_category(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return HAL_CAT_NONE;
    return gpio_slot(pin)[GPIO_CATEGORY];
}

// Weak default — overridden by board pins.c
__attribute__((weak))
void hal_init_pin_categories(void) {
    // No board table → categories stay zeroed.
}

// ── Dirty flag implementation ──

void hal_mark_gpio_dirty(uint8_t pin) {
    if (pin < GPIO_MAX_PINS) {
        port_mem.hal_gpio_dirty |= (1U << pin);
        port_mem.hal_change_count++;
    }
}

void hal_mark_analog_dirty(uint8_t pin) {
    if (pin < GPIO_MAX_PINS) {
        port_mem.hal_analog_dirty |= (1U << pin);
        port_mem.hal_change_count++;
    }
}

bool hal_gpio_changed(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return false;
    return (port_mem.hal_gpio_dirty & (1U << pin)) != 0;
}

bool hal_analog_changed(uint8_t pin) {
    if (pin >= GPIO_MAX_PINS) return false;
    return (port_mem.hal_analog_dirty & (1U << pin)) != 0;
}

uint32_t hal_gpio_drain_dirty(void) {
    uint32_t d = port_mem.hal_gpio_dirty;
    port_mem.hal_gpio_dirty = 0;
    return d;
}

uint32_t hal_analog_drain_dirty(void) {
    uint32_t d = port_mem.hal_analog_dirty;
    port_mem.hal_analog_dirty = 0;
    return d;
}

// ── HAL step — scan dirty, latch inputs, clear ──

void hal_step(void) {
    uint32_t dirty = port_mem.hal_gpio_dirty;
    if (dirty) {
        port_mem.hal_change_count++;
        port_mem.hal_gpio_dirty = 0;
        port_mem.state.flags |= PF_HAL_DIRTY;

        while (dirty) {
            int pin = __builtin_ctz(dirty);
            dirty &= dirty - 1;

            uint8_t *slot = gpio_slot(pin);

            // Clear JS_WROTE, set C_READ
            slot[GPIO_FLAGS] = (slot[GPIO_FLAGS] & ~GF_JS_WROTE) | GF_C_READ;

            // Latch input value
            slot[GPIO_LATCHED] = slot[GPIO_VALUE];
        }
    }

    uint32_t analog_dirty = port_mem.hal_analog_dirty;
    if (analog_dirty) {
        port_mem.hal_change_count++;
        port_mem.hal_analog_dirty = 0;
    }
}

// ── WASM exports — JS calls these to set dirty flags ──

__attribute__((export_name("hal_mark_gpio_dirty")))
void _export_hal_mark_gpio_dirty(int pin) {
    hal_mark_gpio_dirty((uint8_t)pin);
}

__attribute__((export_name("hal_mark_analog_dirty")))
void _export_hal_mark_analog_dirty(int pin) {
    hal_mark_analog_dirty((uint8_t)pin);
}

__attribute__((export_name("hal_get_change_count")))
uint32_t _export_hal_get_change_count(void) {
    return port_mem.hal_change_count;
}
