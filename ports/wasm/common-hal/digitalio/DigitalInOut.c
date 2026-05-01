// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/digitalio/DigitalInOut.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// DigitalInOut.c — Virtual GPIO via direct memory access.
//
// All pin state lives in port_mem.hal_gpio (MEMFS-in-linear-memory).
// Each pin occupies a 12-byte slot accessed via gpio_slot(pin).
// C writes are immediate — no FD calls, no lseek/read/write.
//
// Slot layout (12 bytes per pin — see port/constants.h):
//   [0] enabled    (int8: -1=never_reset, 0=disabled, 1=enabled)
//   [1] direction  (uint8: 0=input, 1=output, 2=output_open_drain)
//   [2] value      (uint8: 0/1)
//   [3] pull       (uint8: 0=none, 1=up, 2=down)
//   [4] role       (uint8: ROLE_*)
//   [5] flags      (uint8: GF_*)
//   [6] category   (uint8: HAL_CAT_*)
//   [7] latched    (uint8: captured input value)
//   [8-11] reserved

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "port/hal.h"
#include "port/port_memory.h"
#include "port/constants.h"
#include "py/runtime.h"

#include <string.h>

// ── WASM imports: pin listener registration ──
//
// When Python claims a pin as input, C tells JS to attach a DOM event
// listener (e.g., click/mousedown on the board image element for that
// pin).  When the user interacts, JS fires the listener which pushes
// an event into the event ring.  The DOM event system IS the
// interrupt controller — no polling or dirty-flag diffing needed.
//
// JS implements these in the port import object (circuitpython.mjs).
// If JS doesn't provide the import, the weak fallback is a no-op.
__attribute__((import_module("port"), import_name("registerPinListener")))
extern void port_register_pin_listener(int pin);

__attribute__((import_module("port"), import_name("unregisterPinListener")))
extern void port_unregister_pin_listener(int pin);

// ──────────────────────────────────────────────────────────────────────

digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    uint8_t *slot = gpio_slot(pin->number);
    memset(slot, 0, GPIO_SLOT_SIZE);
    slot[GPIO_ENABLED] = HAL_ENABLED_YES;
    slot[GPIO_ROLE] = ROLE_DIGITAL_IN;

    // Tell JS to attach a DOM event listener for this pin.
    // JS maps pin number -> board image element and wires
    // mousedown/mouseup -> events.
    port_register_pin_listener(pin->number);

    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }
    // Detach DOM event listener before clearing pin state.
    port_unregister_pin_listener(self->pin->number);

    uint8_t *slot = gpio_slot(self->pin->number);
    memset(slot, 0, GPIO_SLOT_SIZE);  // enabled=0
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    uint8_t *slot = gpio_slot(self->pin->number);
    return slot[GPIO_DIRECTION] == HAL_DIR_INPUT
        ? DIRECTION_INPUT : DIRECTION_OUTPUT;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    uint8_t *slot = gpio_slot(self->pin->number);
    slot[GPIO_DIRECTION] = HAL_DIR_INPUT;
    slot[GPIO_PULL] = (uint8_t)pull;
    // Simulate pull resistor: pull-up reads HIGH, pull-down reads LOW
    // until an external source (JS setInputValue) drives it otherwise.
    if (pull == PULL_UP) {
        slot[GPIO_VALUE] = 1;
    } else if (pull == PULL_DOWN) {
        slot[GPIO_VALUE] = 0;
    }
    slot[GPIO_ROLE] = ROLE_DIGITAL_IN;
    return DIGITALINOUT_OK;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self, bool value,
    digitalio_drive_mode_t drive_mode) {
    uint8_t *slot = gpio_slot(self->pin->number);
    slot[GPIO_DIRECTION] = (drive_mode == DRIVE_MODE_OPEN_DRAIN)
        ? HAL_DIR_OUTPUT_OPEN_DRAIN : HAL_DIR_OUTPUT;
    slot[GPIO_VALUE] = value ? 1 : 0;
    slot[GPIO_ROLE] = ROLE_DIGITAL_OUT;
    return DIGITALINOUT_OK;
}

bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    uint8_t *slot = gpio_slot(self->pin->number);
    uint8_t flags = slot[GPIO_FLAGS];

    // If the port latched an input value (like a GPIO interrupt capture),
    // return the latched value instead of the live value.  This ensures
    // the VM sees a button press even if JS already released (mouseup)
    // before the VM woke from time.sleep().
    if (flags & GF_LATCHED) {
        uint8_t val = slot[GPIO_LATCHED];
        slot[GPIO_FLAGS] = (flags & ~(GF_LATCHED | GF_JS_WROTE)) | GF_C_READ;
        return val != 0;
    }

    // Normal read
    if (flags & GF_JS_WROTE) {
        slot[GPIO_FLAGS] = (flags & ~GF_JS_WROTE) | GF_C_READ;
    }
    return slot[GPIO_VALUE] != 0;
}

void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self,
    bool value) {
    uint8_t *slot = gpio_slot(self->pin->number);
    slot[GPIO_VALUE] = value ? 1 : 0;
    slot[GPIO_FLAGS] |= GF_C_WROTE;
}

digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {
    uint8_t *slot = gpio_slot(self->pin->number);
    return (digitalio_pull_t)slot[GPIO_PULL];
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    uint8_t *slot = gpio_slot(self->pin->number);
    slot[GPIO_PULL] = (uint8_t)pull;
    if (pull == PULL_UP) {
        slot[GPIO_VALUE] = 1;
    } else if (pull == PULL_DOWN) {
        slot[GPIO_VALUE] = 0;
    }
    return DIGITALINOUT_OK;
}

digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {
    uint8_t *slot = gpio_slot(self->pin->number);
    return slot[GPIO_DIRECTION] == HAL_DIR_OUTPUT_OPEN_DRAIN
        ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self, digitalio_drive_mode_t drive_mode) {
    uint8_t *slot = gpio_slot(self->pin->number);
    slot[GPIO_DIRECTION] = (drive_mode == DRIVE_MODE_OPEN_DRAIN)
        ? HAL_DIR_OUTPUT_OPEN_DRAIN : HAL_DIR_OUTPUT;
    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    uint8_t *slot = gpio_slot(self->pin->number);
    slot[GPIO_ENABLED] = (uint8_t)(int8_t)HAL_ENABLED_NEVER_RESET;
    never_reset_pin_number(self->pin->number);
}
