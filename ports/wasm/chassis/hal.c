/*
 * chassis/hal.c — HAL implementation: claim/release, dirty scan, latch.
 */

#include "hal.h"
#include "port_memory.h"
#include "ffi_imports.h"
#include "chassis_constants.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Init / Reset                                                        */
/* ------------------------------------------------------------------ */

void hal_init(void) {
    memset(port_mem.hal_gpio, 0, sizeof(port_mem.hal_gpio));
    memset(port_mem.hal_analog, 0, sizeof(port_mem.hal_analog));
    port_mem.gpio_dirty = 0;
    port_mem.analog_dirty = 0;
    port_mem.change_count = 0;
}

/* ------------------------------------------------------------------ */
/* Claim / Release                                                     */
/* ------------------------------------------------------------------ */

bool hal_claim_pin(uint8_t pin, uint8_t role) {
    if (pin >= PORT_MAX_PINS || role == ROLE_UNCLAIMED) return false;

    uint8_t *slot = gpio_slot(pin);
    uint8_t current_role = slot[GPIO_OFF_ROLE];

    /* Already claimed by same role = OK (idempotent) */
    if (current_role == role) return true;

    /* Claimed by different role = fail */
    if (current_role != ROLE_UNCLAIMED) return false;

    slot[GPIO_OFF_ROLE] = role;
    slot[GPIO_OFF_ENABLED] = 1;
    slot[GPIO_OFF_FLAGS] |= FLAG_C_WROTE;

    /* Set direction based on role */
    if (role == ROLE_DIGITAL_OUT) {
        slot[GPIO_OFF_DIRECTION] = 1;  /* output */
    } else if (role == ROLE_DIGITAL_IN || role == ROLE_ADC) {
        slot[GPIO_OFF_DIRECTION] = 0;  /* input */
    }

    return true;
}

void hal_release_pin(uint8_t pin) {
    if (pin >= PORT_MAX_PINS) return;

    uint8_t *slot = gpio_slot(pin);
    slot[GPIO_OFF_ROLE] = ROLE_UNCLAIMED;
    slot[GPIO_OFF_ENABLED] = 0;
    slot[GPIO_OFF_FLAGS] = FLAG_C_WROTE;
    slot[GPIO_OFF_VALUE] = 0;
    slot[GPIO_OFF_PULL] = 0;
    slot[GPIO_OFF_LATCHED] = 0;
}

void hal_release_all(void) {
    for (int i = 0; i < PORT_MAX_PINS; i++) {
        uint8_t *slot = gpio_slot(i);
        if (slot[GPIO_OFF_ROLE] != ROLE_UNCLAIMED) {
            hal_release_pin(i);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Query                                                               */
/* ------------------------------------------------------------------ */

bool hal_pin_is_claimed(uint8_t pin) {
    if (pin >= PORT_MAX_PINS) return false;
    return gpio_slot(pin)[GPIO_OFF_ROLE] != ROLE_UNCLAIMED;
}

uint8_t hal_pin_role(uint8_t pin) {
    if (pin >= PORT_MAX_PINS) return ROLE_UNCLAIMED;
    return gpio_slot(pin)[GPIO_OFF_ROLE];
}

uint32_t hal_claimed_count(void) {
    uint32_t count = 0;
    for (int i = 0; i < PORT_MAX_PINS; i++) {
        if (gpio_slot(i)[GPIO_OFF_ROLE] != ROLE_UNCLAIMED) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Read / Write                                                        */
/* ------------------------------------------------------------------ */

void hal_write_pin(uint8_t pin, uint8_t value) {
    if (pin >= PORT_MAX_PINS) return;
    uint8_t *slot = gpio_slot(pin);
    slot[GPIO_OFF_VALUE] = value ? 1 : 0;
    slot[GPIO_OFF_FLAGS] = (slot[GPIO_OFF_FLAGS] & ~FLAG_JS_WROTE) | FLAG_C_WROTE;
    /* Notify JS that a pin value changed — JS reads from MEMFS */
    ffi_notify(NOTIFY_PIN_CHANGED, pin, value ? 1 : 0, 0);
}

uint8_t hal_read_pin(uint8_t pin) {
    if (pin >= PORT_MAX_PINS) return 0;
    uint8_t *slot = gpio_slot(pin);
    /* For inputs, return latched value; for outputs, return current value */
    if (slot[GPIO_OFF_DIRECTION] == 0) {
        return slot[GPIO_OFF_LATCHED];
    }
    return slot[GPIO_OFF_VALUE];
}

/* ------------------------------------------------------------------ */
/* HAL step — scan dirty flags, latch, track                           */
/* ------------------------------------------------------------------ */

void hal_step(void) {
    uint64_t dirty = port_mem.gpio_dirty;
    if (dirty) {
        port_mem.change_count++;
        port_mem.gpio_dirty = 0;
        port_mem.state.flags |= PORT_FLAG_HAL_DIRTY;

        while (dirty) {
            int pin = __builtin_ctzll(dirty);
            dirty &= dirty - 1;

            uint8_t *slot = gpio_slot(pin);
            uint8_t value = slot[GPIO_OFF_VALUE];

            /* Clear JS_WROTE, set C_READ */
            slot[GPIO_OFF_FLAGS] = (slot[GPIO_OFF_FLAGS] & ~FLAG_JS_WROTE)
                                   | FLAG_C_READ;

            /* Latch input value */
            slot[GPIO_OFF_LATCHED] = value;
        }
    }

    uint64_t analog_dirty = port_mem.analog_dirty;
    if (analog_dirty) {
        port_mem.change_count++;
        port_mem.analog_dirty = 0;
    }
}
