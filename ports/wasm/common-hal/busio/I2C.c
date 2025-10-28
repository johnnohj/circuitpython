// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// I2C implementation for WASM port

#include "common-hal/busio/I2C.h"
#include "shared-bindings/busio/I2C.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include <emscripten.h>
#include <string.h>

// Maximum number of I2C buses
#define MAX_I2C_BUSES 8

// Maximum buffer size for I2C transactions
#define I2C_BUFFER_SIZE 256

// I2C device state - tracks data for each device address
typedef struct {
    uint8_t registers[256];  // 256 byte register space per device
    bool active;              // Whether this device exists/responds
} i2c_device_state_t;

// I2C bus state
typedef struct {
    uint8_t scl_pin;
    uint8_t sda_pin;
    uint32_t frequency;
    bool enabled;
    bool locked;
    bool never_reset;  // If true, don't reset this bus during soft reset

    // Virtual I2C devices on this bus (128 possible 7-bit addresses)
    i2c_device_state_t devices[128];

    // Last transaction buffers (for JavaScript to read/modify)
    uint8_t last_write_addr;
    uint8_t last_write_data[I2C_BUFFER_SIZE];
    uint16_t last_write_len;

    uint8_t last_read_addr;
    uint8_t last_read_data[I2C_BUFFER_SIZE];
    uint16_t last_read_len;
} i2c_bus_state_t;

// Global I2C bus state array
i2c_bus_state_t i2c_buses[MAX_I2C_BUSES];

EMSCRIPTEN_KEEPALIVE
i2c_bus_state_t* get_i2c_state_ptr(void) {
    return i2c_buses;
}

void busio_reset_i2c_state(void) {
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        // Skip buses marked as never_reset (e.g., used by displayio or supervisor)
        if (i2c_buses[i].never_reset) {
            continue;
        }

        i2c_buses[i].scl_pin = 0xFF;
        i2c_buses[i].sda_pin = 0xFF;
        i2c_buses[i].frequency = 100000;  // 100kHz default
        i2c_buses[i].enabled = false;
        i2c_buses[i].locked = false;
        i2c_buses[i].last_write_len = 0;
        i2c_buses[i].last_read_len = 0;

        // Reset all devices on this bus
        for (int j = 0; j < 128; j++) {
            i2c_buses[i].devices[j].active = false;
            memset(i2c_buses[i].devices[j].registers, 0, 256);
        }
    }
}

// Find an I2C bus by pin pair, or return -1 if not found
static int8_t find_i2c_bus(uint8_t scl_pin, uint8_t sda_pin) {
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        if (i2c_buses[i].enabled &&
            i2c_buses[i].scl_pin == scl_pin &&
            i2c_buses[i].sda_pin == sda_pin) {
            return i;
        }
    }
    return -1;
}

// Find a free I2C bus slot
static int8_t find_free_i2c_bus(void) {
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        if (!i2c_buses[i].enabled) {
            return i;
        }
    }
    return -1;
}

void common_hal_busio_i2c_construct(busio_i2c_obj_t *self,
    const mcu_pin_obj_t *scl, const mcu_pin_obj_t *sda,
    uint32_t frequency, uint32_t timeout) {

    // Claim the pins
    claim_pin(scl);
    claim_pin(sda);

    self->scl = scl;
    self->sda = sda;
    self->has_lock = false;

    // Find or create I2C bus for these pins
    int8_t bus_idx = find_i2c_bus(scl->number, sda->number);
    if (bus_idx < 0) {
        bus_idx = find_free_i2c_bus();
        if (bus_idx < 0) {
            mp_raise_RuntimeError(MP_ERROR_TEXT("All I2C buses in use"));
        }

        // Initialize new bus
        i2c_buses[bus_idx].scl_pin = scl->number;
        i2c_buses[bus_idx].sda_pin = sda->number;
        i2c_buses[bus_idx].frequency = frequency;
        i2c_buses[bus_idx].enabled = true;
        i2c_buses[bus_idx].locked = false;
        i2c_buses[bus_idx].never_reset = false;
    }
}

void common_hal_busio_i2c_deinit(busio_i2c_obj_t *self) {
    if (common_hal_busio_i2c_deinited(self)) {
        return;
    }

    // Find and disable the bus if no other objects are using it
    int8_t bus_idx = find_i2c_bus(self->scl->number, self->sda->number);
    if (bus_idx >= 0) {
        i2c_buses[bus_idx].enabled = false;
    }

    reset_pin_number(self->scl->number);
    reset_pin_number(self->sda->number);
    self->scl = NULL;
    self->sda = NULL;
}

bool common_hal_busio_i2c_deinited(busio_i2c_obj_t *self) {
    return self->scl == NULL;
}

void common_hal_busio_i2c_mark_deinit(busio_i2c_obj_t *self) {
    self->scl = NULL;
    self->sda = NULL;
}

bool common_hal_busio_i2c_try_lock(busio_i2c_obj_t *self) {
    if (self->has_lock) {
        return false;
    }

    int8_t bus_idx = find_i2c_bus(self->scl->number, self->sda->number);
    if (bus_idx >= 0 && !i2c_buses[bus_idx].locked) {
        i2c_buses[bus_idx].locked = true;
        self->has_lock = true;
        return true;
    }
    return false;
}

bool common_hal_busio_i2c_has_lock(busio_i2c_obj_t *self) {
    return self->has_lock;
}

void common_hal_busio_i2c_unlock(busio_i2c_obj_t *self) {
    if (!self->has_lock) {
        return;
    }

    int8_t bus_idx = find_i2c_bus(self->scl->number, self->sda->number);
    if (bus_idx >= 0) {
        i2c_buses[bus_idx].locked = false;
    }
    self->has_lock = false;
}

bool common_hal_busio_i2c_probe(busio_i2c_obj_t *self, uint8_t addr) {
    if (addr >= 128) {
        return false;
    }

    int8_t bus_idx = find_i2c_bus(self->scl->number, self->sda->number);
    if (bus_idx < 0) {
        return false;
    }

    // Check if device is active
    return i2c_buses[bus_idx].devices[addr].active;
}

uint8_t common_hal_busio_i2c_write(busio_i2c_obj_t *self, uint16_t address,
    const uint8_t *data, size_t len) {

    if (address >= 128) {
        return MP_EINVAL;
    }

    int8_t bus_idx = find_i2c_bus(self->scl->number, self->sda->number);
    if (bus_idx < 0) {
        return MP_ENODEV;
    }

    // Check if device exists
    if (!i2c_buses[bus_idx].devices[address].active) {
        return MP_ENODEV;
    }

    // Limit write length
    if (len > I2C_BUFFER_SIZE) {
        len = I2C_BUFFER_SIZE;
    }

    // Store last write for JavaScript access
    i2c_buses[bus_idx].last_write_addr = address;
    memcpy(i2c_buses[bus_idx].last_write_data, data, len);
    i2c_buses[bus_idx].last_write_len = len;

    // If data starts with register address, write to that register
    if (len >= 2) {
        uint8_t reg_addr = data[0];
        memcpy(&i2c_buses[bus_idx].devices[address].registers[reg_addr],
               &data[1], len - 1);
    }

    return 0;  // Success
}

uint8_t common_hal_busio_i2c_read(busio_i2c_obj_t *self, uint16_t address,
    uint8_t *data, size_t len) {

    if (address >= 128) {
        return MP_EINVAL;
    }

    int8_t bus_idx = find_i2c_bus(self->scl->number, self->sda->number);
    if (bus_idx < 0) {
        return MP_ENODEV;
    }

    // Check if device exists
    if (!i2c_buses[bus_idx].devices[address].active) {
        return MP_ENODEV;
    }

    // Limit read length
    if (len > I2C_BUFFER_SIZE) {
        len = I2C_BUFFER_SIZE;
    }

    // Read from device registers (starting from register 0)
    memcpy(data, i2c_buses[bus_idx].devices[address].registers, len);

    // Store last read for JavaScript access
    i2c_buses[bus_idx].last_read_addr = address;
    memcpy(i2c_buses[bus_idx].last_read_data, data, len);
    i2c_buses[bus_idx].last_read_len = len;

    return 0;  // Success
}

uint8_t common_hal_busio_i2c_write_read(busio_i2c_obj_t *self, uint16_t address,
    uint8_t *out_data, size_t out_len, uint8_t *in_data, size_t in_len) {

    if (address >= 128) {
        return MP_EINVAL;
    }

    int8_t bus_idx = find_i2c_bus(self->scl->number, self->sda->number);
    if (bus_idx < 0) {
        return MP_ENODEV;
    }

    // Check if device exists
    if (!i2c_buses[bus_idx].devices[address].active) {
        return MP_ENODEV;
    }

    // Typical I2C pattern: write register address, then read data
    uint8_t reg_addr = (out_len > 0) ? out_data[0] : 0;

    // Limit read length
    if (in_len > I2C_BUFFER_SIZE) {
        in_len = I2C_BUFFER_SIZE;
    }

    // Read from specified register
    memcpy(in_data, &i2c_buses[bus_idx].devices[address].registers[reg_addr], in_len);

    // Store transaction info
    i2c_buses[bus_idx].last_write_addr = address;
    if (out_len <= I2C_BUFFER_SIZE) {
        memcpy(i2c_buses[bus_idx].last_write_data, out_data, out_len);
        i2c_buses[bus_idx].last_write_len = out_len;
    }

    i2c_buses[bus_idx].last_read_addr = address;
    memcpy(i2c_buses[bus_idx].last_read_data, in_data, in_len);
    i2c_buses[bus_idx].last_read_len = in_len;

    return 0;  // Success
}

void common_hal_busio_i2c_never_reset(busio_i2c_obj_t *self) {
    // Mark this I2C bus as never_reset so it persists across soft resets
    // This is important for displays and other supervisor-managed peripherals
    uint8_t tx_pin = (self->scl != NULL) ? self->scl->number : 0xFF;
    uint8_t rx_pin = (self->sda != NULL) ? self->sda->number : 0xFF;

    int8_t bus_idx = find_i2c_bus(tx_pin, rx_pin);
    if (bus_idx >= 0) {
        i2c_buses[bus_idx].never_reset = true;

        // Also mark the pins as never_reset
        if (self->scl != NULL) {
            never_reset_pin_number(self->scl->number);
        }
        if (self->sda != NULL) {
            never_reset_pin_number(self->sda->number);
        }
    }
}
