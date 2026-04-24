/*
 * DigitalInOut.c — Virtual GPIO via /hal/gpio fd endpoint.
 *
 * All pin state lives at the /hal/gpio WASI fd in MEMFS.
 * Each pin occupies a 12-byte slot at offset pin_number * SLOT_SIZE.
 * wasi-memfs.js manages the backing store on the JS side.
 *
 * Slot layout (12 bytes per pin — see supervisor/hal.h):
 *   [0] enabled    (int8: -1=never_reset, 0=disabled, 1=enabled)
 *   [1] direction  (uint8: 0=input, 1=output, 2=output_open_drain)
 *   [2] value      (uint8: 0/1)
 *   [3] pull       (uint8: 0=none, 1=up, 2=down)
 *   [4] role       (uint8: HAL_ROLE_*)
 *   [5] flags      (uint8: HAL_FLAG_*)
 *   [6] category   (uint8: HAL_CAT_*)
 *   [7] latched    (uint8: captured input value)
 *   [8-11] reserved
 */

#include "common-hal/digitalio/DigitalInOut.h"
#include "shared-bindings/digitalio/DigitalInOut.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "supervisor/hal.h"
#include "py/runtime.h"

#include <string.h>
#include <unistd.h>

/* ── WASM imports: pin listener registration ──
 *
 * When Python claims a pin as input, C tells JS to attach a DOM event
 * listener (e.g., click/mousedown on the board image element for that
 * pin).  When the user interacts, JS fires the listener which pushes
 * SH_EVT_HW_CHANGE into the event ring.  The DOM event system IS the
 * interrupt controller — no polling or dirty-flag diffing needed.
 *
 * JS implements these in the port import object (circuitpython.mjs).
 * If JS doesn't provide the import, the weak fallback is a no-op. */
__attribute__((import_module("port"), import_name("registerPinListener")))
extern void port_register_pin_listener(int pin);

__attribute__((import_module("port"), import_name("unregisterPinListener")))
extern void port_unregister_pin_listener(int pin);

/* Read a pin's slot from the /hal/gpio fd */
static void _read_pin(uint8_t pin, uint8_t slot[HAL_GPIO_SLOT_SIZE]) {
    int fd = hal_gpio_fd();
    if (fd < 0) {
        memset(slot, 0, HAL_GPIO_SLOT_SIZE);
        return;
    }
    lseek(fd, pin * HAL_GPIO_SLOT_SIZE, SEEK_SET);
    ssize_t n = read(fd, slot, HAL_GPIO_SLOT_SIZE);
    if (n < HAL_GPIO_SLOT_SIZE) {
        memset(slot + (n > 0 ? n : 0), 0, HAL_GPIO_SLOT_SIZE - (n > 0 ? n : 0));
    }
}

/* Write a pin's slot to the /hal/gpio fd */
static void _write_pin(uint8_t pin, const uint8_t slot[HAL_GPIO_SLOT_SIZE]) {
    int fd = hal_gpio_fd();
    if (fd < 0) {
        return;
    }
    lseek(fd, pin * HAL_GPIO_SLOT_SIZE, SEEK_SET);
    write(fd, slot, HAL_GPIO_SLOT_SIZE);
}

/* ------------------------------------------------------------------ */

digitalinout_result_t common_hal_digitalio_digitalinout_construct(
    digitalio_digitalinout_obj_t *self, const mcu_pin_obj_t *pin) {
    self->pin = pin;
    claim_pin(pin);

    uint8_t slot[HAL_GPIO_SLOT_SIZE] = {0};
    slot[HAL_GPIO_OFF_ENABLED] = HAL_ENABLED_YES;
    slot[HAL_GPIO_OFF_ROLE] = HAL_ROLE_DIGITAL_IN;
    _write_pin(pin->number, slot);

    /* Tell JS to attach a DOM event listener for this pin.
     * JS maps pin number → board image element and wires
     * mousedown/mouseup → SH_EVT_HW_CHANGE events. */
    port_register_pin_listener(pin->number);

    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_deinit(digitalio_digitalinout_obj_t *self) {
    if (common_hal_digitalio_digitalinout_deinited(self)) {
        return;
    }
    /* Detach DOM event listener before clearing pin state. */
    port_unregister_pin_listener(self->pin->number);

    uint8_t slot[HAL_GPIO_SLOT_SIZE] = {0}; /* enabled=0 */
    _write_pin(self->pin->number, slot);
    reset_pin_number(self->pin->number);
    self->pin = NULL;
}

bool common_hal_digitalio_digitalinout_deinited(digitalio_digitalinout_obj_t *self) {
    return self->pin == NULL;
}

digitalio_direction_t common_hal_digitalio_digitalinout_get_direction(
    digitalio_digitalinout_obj_t *self) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return slot[HAL_GPIO_OFF_DIRECTION] == HAL_DIR_INPUT
        ? DIRECTION_INPUT : DIRECTION_OUTPUT;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_input(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[HAL_GPIO_OFF_DIRECTION] = HAL_DIR_INPUT;
    slot[HAL_GPIO_OFF_PULL] = (uint8_t)pull;
    /* Simulate pull resistor: pull-up reads HIGH, pull-down reads LOW
     * until an external source (JS setInputValue) drives it otherwise. */
    if (pull == PULL_UP) {
        slot[HAL_GPIO_OFF_VALUE] = 1;
    } else if (pull == PULL_DOWN) {
        slot[HAL_GPIO_OFF_VALUE] = 0;
    }
    slot[HAL_GPIO_OFF_ROLE] = HAL_ROLE_DIGITAL_IN;
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

digitalinout_result_t common_hal_digitalio_digitalinout_switch_to_output(
    digitalio_digitalinout_obj_t *self, bool value,
    digitalio_drive_mode_t drive_mode) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[HAL_GPIO_OFF_DIRECTION] = (drive_mode == DRIVE_MODE_OPEN_DRAIN)
        ? HAL_DIR_OUTPUT_OPEN_DRAIN : HAL_DIR_OUTPUT;
    slot[HAL_GPIO_OFF_VALUE] = value ? 1 : 0;
    slot[HAL_GPIO_OFF_ROLE] = HAL_ROLE_DIGITAL_OUT;
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    uint8_t pin_num = self->pin->number;
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(pin_num, slot);

    uint8_t flags = slot[HAL_GPIO_OFF_FLAGS];

    /* If the port latched an input value (like a GPIO interrupt capture),
     * return the latched value instead of the live MEMFS value.  This
     * ensures the VM sees a button press even if JS already released
     * (mouseup) before the VM woke from time.sleep(). */
    if (flags & HAL_FLAG_LATCHED) {
        uint8_t val = slot[HAL_GPIO_OFF_LATCHED];
        slot[HAL_GPIO_OFF_FLAGS] = (flags & ~(HAL_FLAG_LATCHED | HAL_FLAG_JS_WROTE))
                                 | HAL_FLAG_C_READ;
        _write_pin(pin_num, slot);
        return val != 0;
    }

    /* Normal read from MEMFS */
    if (flags & HAL_FLAG_JS_WROTE) {
        slot[HAL_GPIO_OFF_FLAGS] = (flags & ~HAL_FLAG_JS_WROTE) | HAL_FLAG_C_READ;
        _write_pin(pin_num, slot);
    }
    return slot[HAL_GPIO_OFF_VALUE] != 0;
}

void common_hal_digitalio_digitalinout_set_value(digitalio_digitalinout_obj_t *self,
    bool value) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[HAL_GPIO_OFF_VALUE] = value ? 1 : 0;
    slot[HAL_GPIO_OFF_FLAGS] |= HAL_FLAG_C_WROTE;
    _write_pin(self->pin->number, slot);
}

digitalio_pull_t common_hal_digitalio_digitalinout_get_pull(
    digitalio_digitalinout_obj_t *self) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return (digitalio_pull_t)slot[HAL_GPIO_OFF_PULL];
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_pull(
    digitalio_digitalinout_obj_t *self, digitalio_pull_t pull) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[HAL_GPIO_OFF_PULL] = (uint8_t)pull;
    if (pull == PULL_UP) {
        slot[HAL_GPIO_OFF_VALUE] = 1;
    } else if (pull == PULL_DOWN) {
        slot[HAL_GPIO_OFF_VALUE] = 0;
    }
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

digitalio_drive_mode_t common_hal_digitalio_digitalinout_get_drive_mode(
    digitalio_digitalinout_obj_t *self) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    return slot[HAL_GPIO_OFF_DIRECTION] == HAL_DIR_OUTPUT_OPEN_DRAIN
        ? DRIVE_MODE_OPEN_DRAIN : DRIVE_MODE_PUSH_PULL;
}

digitalinout_result_t common_hal_digitalio_digitalinout_set_drive_mode(
    digitalio_digitalinout_obj_t *self, digitalio_drive_mode_t drive_mode) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[HAL_GPIO_OFF_DIRECTION] = (drive_mode == DRIVE_MODE_OPEN_DRAIN)
        ? HAL_DIR_OUTPUT_OPEN_DRAIN : HAL_DIR_OUTPUT;
    _write_pin(self->pin->number, slot);
    return DIGITALINOUT_OK;
}

void common_hal_digitalio_digitalinout_never_reset(digitalio_digitalinout_obj_t *self) {
    uint8_t slot[HAL_GPIO_SLOT_SIZE];
    _read_pin(self->pin->number, slot);
    slot[HAL_GPIO_OFF_ENABLED] = (uint8_t)(int8_t)HAL_ENABLED_NEVER_RESET;
    _write_pin(self->pin->number, slot);
    never_reset_pin_number(self->pin->number);
}
