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
#include "proxy_c.h"
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

    // Rich path: Optional JsProxy for events (NULL if no web app listeners)
    // When this exists, JS I2C bus object is the source of truth for transactions
    // C code syncs to it via store_attr(), triggering automatic onChange events
    mp_obj_jsproxy_t *js_bus;
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
        i2c_buses[i].js_bus = NULL;  // No JsProxy initially

        // Reset all devices on this bus
        for (int j = 0; j < 128; j++) {
            i2c_buses[i].devices[j].active = false;
            memset(i2c_buses[i].devices[j].registers, 0, 256);
        }
    }
}

// ========== Peripheral Hook Integration Functions ==========

// Create a JS I2C bus object via peripheral hook and add it to proxy system
// This uses the layered architecture where peripherals can be attached by applications
EM_JS(int, i2c_create_js_bus_proxy, (int bus_index), {
    // Try to get I2C peripheral via hook system (preferred method)
    if (Module.hasPeripheral && Module.hasPeripheral('i2c')) {
        const i2cPeripheral = Module.getPeripheral('i2c');

        // Check if peripheral has getBus method
        if (i2cPeripheral && typeof i2cPeripheral.getBus === 'function') {
            const bus = i2cPeripheral.getBus(bus_index);
            if (bus) {
                return proxy_js_add_obj(bus);
            }
        }
    }

    // Fallback: Try legacy _circuitPythonBoard for backward compatibility
    if (Module._circuitPythonBoard && Module._circuitPythonBoard.i2c) {
        const bus = Module._circuitPythonBoard.i2c.getBus(bus_index);
        if (bus) {
            return proxy_js_add_obj(bus);
        }
    }

    // No peripheral attached - will use state array simulation only (fast path)
    return -1;
});

// Get current timestamp in milliseconds
EM_JS(double, i2c_get_timestamp_ms, (void), {
    return Date.now();
});

// Check if device exists on I2C bus via peripheral hook
// Returns: 1 if device exists, 0 if not, -1 if peripheral doesn't support probing
EM_JS(int, i2c_peripheral_probe, (int bus_index, uint8_t addr), {
    // Try peripheral hook first
    if (Module.hasPeripheral && Module.hasPeripheral('i2c')) {
        const i2cPeripheral = Module.getPeripheral('i2c');

        // Check if peripheral has probe method
        if (i2cPeripheral && typeof i2cPeripheral.probe === 'function') {
            return i2cPeripheral.probe(addr) ? 1 : 0;
        }
    }

    // Fallback: Try legacy board.i2c
    if (Module._circuitPythonBoard && Module._circuitPythonBoard.i2c) {
        const bus = Module._circuitPythonBoard.i2c.getBus(bus_index);
        if (bus && typeof bus._notifyProbe === 'function') {
            // Legacy path - just notify, use state array for result
            return -1;
        }
    }

    // No peripheral - use state array
    return -1;
});

// Perform I2C read via peripheral hook
// Returns: 0 on success, error code on failure, -1 if peripheral doesn't support reads
EM_JS(int, i2c_peripheral_read, (int bus_index, uint8_t addr, uint8_t *buffer, size_t len), {
    // Try peripheral hook first
    if (Module.hasPeripheral && Module.hasPeripheral('i2c')) {
        const i2cPeripheral = Module.getPeripheral('i2c');

        // Check if peripheral has read method
        if (i2cPeripheral && typeof i2cPeripheral.read === 'function') {
            try {
                // Peripheral read should return Uint8Array or null
                const data = i2cPeripheral.read(addr, 0, len);  // register 0 for simple read
                if (data && data.length > 0) {
                    // Copy data from JS to WASM memory
                    const heapView = new Uint8Array(Module.HEAPU8.buffer, buffer, len);
                    heapView.set(data.subarray(0, Math.min(len, data.length)));
                    return 0;  // Success
                }
                return 1;  // Device didn't respond
            } catch (e) {
                console.error('[I2C] Peripheral read error:', e);
                return 2;  // Error
            }
        }
    }

    // No peripheral - use state array
    return -1;
});

// Perform I2C write via peripheral hook
// Returns: 0 on success, error code on failure, -1 if peripheral doesn't support writes
EM_JS(int, i2c_peripheral_write, (int bus_index, uint8_t addr, const uint8_t *data, size_t len), {
    // Try peripheral hook first
    if (Module.hasPeripheral && Module.hasPeripheral('i2c')) {
        const i2cPeripheral = Module.getPeripheral('i2c');

        // Check if peripheral has write method
        if (i2cPeripheral && typeof i2cPeripheral.write === 'function') {
            try {
                // Copy data from WASM to JS
                const buffer = new Uint8Array(Module.HEAPU8.buffer, data, len);
                const dataCopy = new Uint8Array(buffer);  // Make a copy to avoid memory issues

                // Peripheral write: (address, register, data)
                // First byte is register, rest is data
                const register = len > 0 ? dataCopy[0] : 0;
                const writeData = len > 1 ? dataCopy.subarray(1) : new Uint8Array(0);

                i2cPeripheral.write(addr, register, writeData);
                return 0;  // Success
            } catch (e) {
                console.error('[I2C] Peripheral write error:', e);
                return 2;  // Error
            }
        }
    }

    // No peripheral - use state array
    return -1;
});

// Perform I2C write-then-read via peripheral hook
// Returns: 0 on success, error code on failure, -1 if peripheral doesn't support write_read
EM_JS(int, i2c_peripheral_write_read, (int bus_index, uint8_t addr,
    const uint8_t *out_data, size_t out_len, uint8_t *in_data, size_t in_len), {
    // Try peripheral hook first
    if (Module.hasPeripheral && Module.hasPeripheral('i2c')) {
        const i2cPeripheral = Module.getPeripheral('i2c');

        // Check if peripheral has read method (we use write to set register, then read)
        if (i2cPeripheral && typeof i2cPeripheral.read === 'function') {
            try {
                // Copy write data (typically register address)
                const outBuffer = new Uint8Array(Module.HEAPU8.buffer, out_data, out_len);
                const register = out_len > 0 ? outBuffer[0] : 0;

                // Read from peripheral
                const data = i2cPeripheral.read(addr, register, in_len);
                if (data && data.length > 0) {
                    // Copy data from JS to WASM memory
                    const inBuffer = new Uint8Array(Module.HEAPU8.buffer, in_data, in_len);
                    inBuffer.set(data.subarray(0, Math.min(in_len, data.length)));
                    return 0;  // Success
                }
                return 1;  // Device didn't respond
            } catch (e) {
                console.error('[I2C] Peripheral write_read error:', e);
                return 2;  // Error
            }
        }
    }

    // No peripheral - use state array
    return -1;
});

// Helper to sync transaction data to JS proxy
static inline void i2c_sync_transaction_to_js(mp_obj_jsproxy_t *js_bus,
    uint8_t addr, const uint8_t *data, size_t len, bool is_write) {
    if (js_bus == NULL) {
        return;
    }

    // Create transaction object as a Python dict
    mp_obj_t transaction_dict = mp_obj_new_dict(5);

    // Add type field
    mp_obj_dict_store(transaction_dict,
        mp_obj_new_str("type", 4),
        mp_obj_new_str(is_write ? "write" : "read", is_write ? 5 : 4));

    // Add address field
    mp_obj_dict_store(transaction_dict,
        mp_obj_new_str("addr", 4),
        mp_obj_new_int(addr));

    // Add data field (as bytes)
    mp_obj_t data_bytes = mp_obj_new_bytes(data, len);
    mp_obj_dict_store(transaction_dict,
        mp_obj_new_str("data", 4),
        data_bytes);

    // Add length field
    mp_obj_dict_store(transaction_dict,
        mp_obj_new_str("length", 6),
        mp_obj_new_int(len));

    // Add timestamp field
    double timestamp = i2c_get_timestamp_ms();
    mp_obj_dict_store(transaction_dict,
        mp_obj_new_str("timestamp", 9),
        mp_obj_new_float(timestamp));

    // Convert to JS and set as property on bus object
    uint32_t value_out[PVN];
    proxy_convert_mp_to_js_obj_cside(transaction_dict, value_out);
    store_attr(js_bus->ref, "lastTransaction", value_out);
}

// Helper to sync device probe to JS proxy
static inline void i2c_sync_probe_to_js(mp_obj_jsproxy_t *js_bus, uint8_t addr, bool found) {
    if (js_bus == NULL) {
        return;
    }

    // Create probe result object
    mp_obj_t probe_dict = mp_obj_new_dict(2);

    mp_obj_dict_store(probe_dict,
        mp_obj_new_str("addr", 4),
        mp_obj_new_int(addr));

    mp_obj_dict_store(probe_dict,
        mp_obj_new_str("found", 5),
        mp_obj_new_bool(found));

    // Convert to JS and set as property
    uint32_t value_out[PVN];
    proxy_convert_mp_to_js_obj_cside(probe_dict, value_out);
    store_attr(js_bus->ref, "lastProbe", value_out);
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

        // Create JsProxy for this bus if not already created
        if (i2c_buses[bus_idx].js_bus == NULL) {
            int jsref = i2c_create_js_bus_proxy(bus_idx);
            if (jsref >= 0) {
                i2c_buses[bus_idx].js_bus = mp_obj_new_jsproxy(jsref);
            }
        }
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

    // Try peripheral hook first
    int peripheral_result = i2c_peripheral_probe(bus_idx, addr);
    bool found;

    if (peripheral_result >= 0) {
        // Peripheral hook handled it
        found = (peripheral_result == 1);
        // Update state array to match peripheral result
        i2c_buses[bus_idx].devices[addr].active = found;
    } else {
        // No peripheral or legacy mode - use state array
        found = i2c_buses[bus_idx].devices[addr].active;
    }

    // Rich path: Sync probe result to JsProxy (triggers automatic probe events)
    i2c_sync_probe_to_js(i2c_buses[bus_idx].js_bus, addr, found);

    return found;
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

    // Try peripheral hook first
    int peripheral_result = i2c_peripheral_write(bus_idx, address, data, len);

    if (peripheral_result == 0) {
        // Peripheral hook successfully handled the write
    } else if (peripheral_result < 0) {
        // No peripheral or legacy mode - use state array
        // If data starts with register address, write to that register
        if (len >= 2) {
            uint8_t reg_addr = data[0];
            memcpy(&i2c_buses[bus_idx].devices[address].registers[reg_addr],
                   &data[1], len - 1);
        }
    } else {
        // Peripheral returned an error
        return MP_EIO;
    }

    // Rich path: Sync to JsProxy (triggers automatic transaction events)
    i2c_sync_transaction_to_js(i2c_buses[bus_idx].js_bus, address, data, len, true);

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

    // Try peripheral hook first
    int peripheral_result = i2c_peripheral_read(bus_idx, address, data, len);

    if (peripheral_result == 0) {
        // Peripheral hook successfully handled the read
        // Data is already in the buffer
    } else if (peripheral_result < 0) {
        // No peripheral or legacy mode - use state array
        memcpy(data, i2c_buses[bus_idx].devices[address].registers, len);
    } else {
        // Peripheral returned an error
        return MP_EIO;
    }

    // Store last read for JavaScript access
    i2c_buses[bus_idx].last_read_addr = address;
    memcpy(i2c_buses[bus_idx].last_read_data, data, len);
    i2c_buses[bus_idx].last_read_len = len;

    // Rich path: Sync to JsProxy (triggers automatic transaction events)
    i2c_sync_transaction_to_js(i2c_buses[bus_idx].js_bus, address, data, len, false);

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

    // Try peripheral hook first
    int peripheral_result = i2c_peripheral_write_read(bus_idx, address,
                                                      out_data, out_len, in_data, in_len);

    if (peripheral_result == 0) {
        // Peripheral hook successfully handled the write_read
        // Data is already in in_data buffer
    } else if (peripheral_result < 0) {
        // No peripheral or legacy mode - use state array
        memcpy(in_data, &i2c_buses[bus_idx].devices[address].registers[reg_addr], in_len);
    } else {
        // Peripheral returned an error
        return MP_EIO;
    }

    // Store transaction info
    i2c_buses[bus_idx].last_write_addr = address;
    if (out_len <= I2C_BUFFER_SIZE) {
        memcpy(i2c_buses[bus_idx].last_write_data, out_data, out_len);
        i2c_buses[bus_idx].last_write_len = out_len;
    }

    i2c_buses[bus_idx].last_read_addr = address;
    memcpy(i2c_buses[bus_idx].last_read_data, in_data, in_len);
    i2c_buses[bus_idx].last_read_len = in_len;

    // Rich path: Sync write and read to JsProxy (triggers automatic transaction events)
    // First sync the write part (register address)
    if (out_len > 0) {
        i2c_sync_transaction_to_js(i2c_buses[bus_idx].js_bus, address, out_data, out_len, true);
    }
    // Then sync the read part (data read from register)
    i2c_sync_transaction_to_js(i2c_buses[bus_idx].js_bus, address, in_data, in_len, false);

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
