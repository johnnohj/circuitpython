// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// SPI implementation for WASM port using state array architecture

#include "common-hal/busio/SPI.h"
#include "shared-bindings/busio/SPI.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "proxy_c.h"
#include <emscripten.h>
#include <string.h>

#define MAX_SPI_BUSES 4
#define SPI_BUFFER_SIZE 256

// SPI bus state - exposed to JavaScript for simulation
typedef struct {
    uint8_t clock_pin;
    uint8_t mosi_pin;
    uint8_t miso_pin;
    uint32_t baudrate;
    uint8_t polarity;
    uint8_t phase;
    uint8_t bits;
    bool enabled;
    bool locked;
    bool never_reset;  // If true, don't reset this bus during soft reset
    bool half_duplex;

    // Last transfer data for JavaScript access
    uint8_t last_write_data[SPI_BUFFER_SIZE];
    uint8_t last_read_data[SPI_BUFFER_SIZE];
    uint16_t last_write_len;
    uint16_t last_read_len;

    // Rich path: Optional JsProxy for events (NULL if no web app listeners)
    // When this exists, JS SPI bus object is the source of truth for transactions
    // C code syncs to it via store_attr(), triggering automatic onChange events
    mp_obj_jsproxy_t *js_spi;
} spi_bus_state_t;

// State array for up to 4 SPI buses
spi_bus_state_t spi_buses[MAX_SPI_BUSES];

// Expose pointer to JavaScript
EMSCRIPTEN_KEEPALIVE
spi_bus_state_t* get_spi_state_ptr(void) {
    return spi_buses;
}

// Helper: Find SPI bus by pins
static int8_t find_spi_bus(uint8_t clock_pin, uint8_t mosi_pin, uint8_t miso_pin) {
    for (int i = 0; i < MAX_SPI_BUSES; i++) {
        if (spi_buses[i].enabled &&
            spi_buses[i].clock_pin == clock_pin &&
            spi_buses[i].mosi_pin == mosi_pin &&
            spi_buses[i].miso_pin == miso_pin) {
            return i;
        }
    }
    return -1;
}

// Reset SPI state (called during port reset)
void busio_reset_spi_state(void) {
    for (int i = 0; i < MAX_SPI_BUSES; i++) {
        // Skip buses marked as never_reset (e.g., used by displayio or external flash)
        if (spi_buses[i].never_reset) {
            continue;
        }

        spi_buses[i].clock_pin = 0xFF;
        spi_buses[i].mosi_pin = 0xFF;
        spi_buses[i].miso_pin = 0xFF;
        spi_buses[i].baudrate = 250000;  // 250kHz default
        spi_buses[i].polarity = 0;
        spi_buses[i].phase = 0;
        spi_buses[i].bits = 8;
        spi_buses[i].enabled = false;
        spi_buses[i].locked = false;
        spi_buses[i].half_duplex = false;
        spi_buses[i].last_write_len = 0;
        spi_buses[i].last_read_len = 0;
        spi_buses[i].js_spi = NULL;  // No JsProxy initially
    }
}

// ========== Peripheral Hook Integration Functions ==========

// Create a JS SPI bus object via peripheral hook and add it to proxy system
// This uses the layered architecture where peripherals can be attached by applications
EM_JS(int, spi_create_js_bus_proxy, (int bus_index), {
    // Try to get SPI peripheral via hook system (preferred method)
    if (Module.hasPeripheral && Module.hasPeripheral('spi')) {
        const spiPeripheral = Module.getPeripheral('spi');

        // Check if peripheral has getBus method
        if (spiPeripheral && typeof spiPeripheral.getBus === 'function') {
            const bus = spiPeripheral.getBus(bus_index);
            if (bus) {
                return proxy_js_add_obj(bus);
            }
        }
    }

    // Fallback: Try legacy _circuitPythonBoard for backward compatibility
    if (Module._circuitPythonBoard && Module._circuitPythonBoard.spi) {
        const bus = Module._circuitPythonBoard.spi.getBus(bus_index);
        if (bus) {
            return proxy_js_add_obj(bus);
        }
    }

    // No peripheral attached - will use state array simulation only (fast path)
    return -1;
});

// Get current timestamp in milliseconds
EM_JS(double, spi_get_timestamp_ms, (void), {
    return Date.now();
});

// Perform SPI transfer via peripheral hook (full-duplex)
// Returns: 0 on success, error code on failure, -1 if peripheral doesn't support transfer
EM_JS(int, spi_peripheral_transfer, (int bus_index, const uint8_t *write_data, uint8_t *read_data, size_t len), {
    // Try peripheral hook first
    if (Module.hasPeripheral && Module.hasPeripheral('spi')) {
        const spiPeripheral = Module.getPeripheral('spi');

        // Check if peripheral has transfer method
        if (spiPeripheral && typeof spiPeripheral.transfer === 'function') {
            try {
                // Copy write data from WASM to JS
                const writeBuffer = new Uint8Array(Module.HEAPU8.buffer, write_data, len);
                const writeData = new Uint8Array(writeBuffer);  // Make a copy

                // Perform transfer
                const readDataJS = spiPeripheral.transfer(writeData);

                if (readDataJS && readDataJS.length > 0) {
                    // Copy read data from JS to WASM
                    const readBuffer = new Uint8Array(Module.HEAPU8.buffer, read_data, len);
                    readBuffer.set(readDataJS.subarray(0, Math.min(len, readDataJS.length)));
                    return 0;  // Success
                }
                return 1;  // No response
            } catch (e) {
                console.error('[SPI] Peripheral transfer error:', e);
                return 2;  // Error
            }
        }
    }

    // No peripheral - use state array
    return -1;
});

// Configure SPI via peripheral hook
// Returns: 0 on success, -1 if peripheral doesn't support configuration
EM_JS(int, spi_peripheral_configure, (int bus_index, uint32_t baudrate, uint8_t polarity, uint8_t phase, uint8_t bits), {
    // Try peripheral hook first
    if (Module.hasPeripheral && Module.hasPeripheral('spi')) {
        const spiPeripheral = Module.getPeripheral('spi');

        // Check if peripheral has configure method
        if (spiPeripheral && typeof spiPeripheral.configure === 'function') {
            try {
                spiPeripheral.configure({
                    frequency: baudrate,
                    polarity: polarity,
                    phase: phase,
                    bits: bits
                });
                return 0;  // Success
            } catch (e) {
                console.error('[SPI] Peripheral configure error:', e);
                return 2;  // Error
            }
        }
    }

    // No peripheral or doesn't support configuration
    return -1;
});

// Helper to sync transaction data to JS proxy
static inline void spi_sync_transaction_to_js(mp_obj_jsproxy_t *js_spi,
    const uint8_t *write_data, size_t write_len,
    const uint8_t *read_data, size_t read_len,
    const char *type) {
    if (js_spi == NULL) {
        return;
    }

    // Create transaction object as a Python dict
    mp_obj_t transaction_dict = mp_obj_new_dict(5);

    // Add type field ("write", "read", or "transfer")
    mp_obj_dict_store(transaction_dict,
        mp_obj_new_str("type", 4),
        mp_obj_new_str(type, strlen(type)));

    // Add write data if present
    if (write_data != NULL && write_len > 0) {
        mp_obj_t write_bytes = mp_obj_new_bytes(write_data, write_len);
        mp_obj_dict_store(transaction_dict,
            mp_obj_new_str("writeData", 9),
            write_bytes);
        mp_obj_dict_store(transaction_dict,
            mp_obj_new_str("writeLen", 8),
            mp_obj_new_int(write_len));
    }

    // Add read data if present
    if (read_data != NULL && read_len > 0) {
        mp_obj_t read_bytes = mp_obj_new_bytes(read_data, read_len);
        mp_obj_dict_store(transaction_dict,
            mp_obj_new_str("readData", 8),
            read_bytes);
        mp_obj_dict_store(transaction_dict,
            mp_obj_new_str("readLen", 7),
            mp_obj_new_int(read_len));
    }

    // Add timestamp field
    double timestamp = spi_get_timestamp_ms();
    mp_obj_dict_store(transaction_dict,
        mp_obj_new_str("timestamp", 9),
        mp_obj_new_float(timestamp));

    // Convert to JS and set as property on bus object
    uint32_t value_out[PVN];
    proxy_convert_mp_to_js_obj_cside(transaction_dict, value_out);
    store_attr(js_spi->ref, "lastTransaction", value_out);
}

void common_hal_busio_spi_construct(busio_spi_obj_t *self,
    const mcu_pin_obj_t *clock, const mcu_pin_obj_t *mosi,
    const mcu_pin_obj_t *miso, bool half_duplex) {

    // Claim pins
    claim_pin(clock);
    if (mosi != NULL) {
        claim_pin(mosi);
    }
    if (miso != NULL) {
        claim_pin(miso);
    }

    self->clock = clock;
    self->MOSI = mosi;
    self->MISO = miso;
    self->has_lock = false;

    uint8_t clock_pin = clock->number;
    uint8_t mosi_pin = (mosi != NULL) ? mosi->number : 0xFF;
    uint8_t miso_pin = (miso != NULL) ? miso->number : 0xFF;

    // Find or create SPI bus
    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);

    if (bus_idx < 0) {
        // Find empty slot
        for (int i = 0; i < MAX_SPI_BUSES; i++) {
            if (!spi_buses[i].enabled) {
                bus_idx = i;
                break;
            }
        }

        if (bus_idx < 0) {
            mp_raise_ValueError(MP_ERROR_TEXT("All SPI peripherals in use"));
        }

        // Initialize new bus
        spi_buses[bus_idx].clock_pin = clock_pin;
        spi_buses[bus_idx].mosi_pin = mosi_pin;
        spi_buses[bus_idx].miso_pin = miso_pin;
        spi_buses[bus_idx].baudrate = 250000;  // 250kHz default
        spi_buses[bus_idx].polarity = 0;
        spi_buses[bus_idx].phase = 0;
        spi_buses[bus_idx].bits = 8;
        spi_buses[bus_idx].enabled = true;
        spi_buses[bus_idx].locked = false;
        spi_buses[bus_idx].never_reset = false;
        spi_buses[bus_idx].half_duplex = half_duplex;

        // Create JsProxy for this bus if not already created
        if (spi_buses[bus_idx].js_spi == NULL) {
            int jsref = spi_create_js_bus_proxy(bus_idx);
            if (jsref >= 0) {
                spi_buses[bus_idx].js_spi = mp_obj_new_jsproxy(jsref);
            }
        }
    }

    // Store configuration in object
    self->baudrate = spi_buses[bus_idx].baudrate;
    self->polarity = spi_buses[bus_idx].polarity;
    self->phase = spi_buses[bus_idx].phase;
    self->bits = spi_buses[bus_idx].bits;
}

void common_hal_busio_spi_deinit(busio_spi_obj_t *self) {
    if (common_hal_busio_spi_deinited(self)) {
        return;
    }

    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx >= 0) {
        spi_buses[bus_idx].enabled = false;
    }

    reset_pin_number(clock_pin);
    if (self->MOSI != NULL) {
        reset_pin_number(mosi_pin);
    }
    if (self->MISO != NULL) {
        reset_pin_number(miso_pin);
    }

    self->clock = NULL;
    self->MOSI = NULL;
    self->MISO = NULL;
}

bool common_hal_busio_spi_deinited(busio_spi_obj_t *self) {
    return self->clock == NULL;
}

bool common_hal_busio_spi_configure(busio_spi_obj_t *self, uint32_t baudrate,
    uint8_t polarity, uint8_t phase, uint8_t bits) {

    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx < 0) {
        return false;
    }

    // Update state array configuration
    spi_buses[bus_idx].baudrate = baudrate;
    spi_buses[bus_idx].polarity = polarity;
    spi_buses[bus_idx].phase = phase;
    spi_buses[bus_idx].bits = bits;

    // Store in object for quick access
    self->baudrate = baudrate;
    self->polarity = polarity;
    self->phase = phase;
    self->bits = bits;

    // Try to configure via peripheral hook
    spi_peripheral_configure(bus_idx, baudrate, polarity, phase, bits);
    // Note: We don't check the return value - peripheral configuration is optional

    return true;
}

bool common_hal_busio_spi_try_lock(busio_spi_obj_t *self) {
    if (self->has_lock) {
        return false;
    }

    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx < 0) {
        return false;
    }

    if (spi_buses[bus_idx].locked) {
        return false;
    }

    spi_buses[bus_idx].locked = true;
    self->has_lock = true;
    return true;
}

bool common_hal_busio_spi_has_lock(busio_spi_obj_t *self) {
    return self->has_lock;
}

void common_hal_busio_spi_unlock(busio_spi_obj_t *self) {
    if (!self->has_lock) {
        return;
    }

    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx >= 0) {
        spi_buses[bus_idx].locked = false;
    }

    self->has_lock = false;
}

bool common_hal_busio_spi_write(busio_spi_obj_t *self, const uint8_t *data, size_t len) {
    if (!self->has_lock) {
        return false;
    }

    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx < 0) {
        return false;
    }

    // Fast path: Store write data for JavaScript access (up to buffer size)
    size_t copy_len = (len > SPI_BUFFER_SIZE) ? SPI_BUFFER_SIZE : len;
    memcpy(spi_buses[bus_idx].last_write_data, data, copy_len);
    spi_buses[bus_idx].last_write_len = copy_len;

    // Rich path: Sync to JsProxy (triggers automatic transaction events)
    spi_sync_transaction_to_js(spi_buses[bus_idx].js_spi, data, copy_len, NULL, 0, "write");

    return true;
}

bool common_hal_busio_spi_read(busio_spi_obj_t *self, uint8_t *data, size_t len, uint8_t write_value) {
    if (!self->has_lock) {
        return false;
    }

    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx < 0) {
        return false;
    }

    // Fast path: For WASM simulation, JavaScript can write to last_read_data
    // to simulate peripheral responses
    size_t copy_len = (len > SPI_BUFFER_SIZE) ? SPI_BUFFER_SIZE : len;
    memcpy(data, spi_buses[bus_idx].last_read_data, copy_len);
    spi_buses[bus_idx].last_read_len = copy_len;

    // Rich path: Sync to JsProxy (triggers automatic transaction events)
    spi_sync_transaction_to_js(spi_buses[bus_idx].js_spi, NULL, 0, data, copy_len, "read");

    return true;
}

bool common_hal_busio_spi_transfer(busio_spi_obj_t *self, const uint8_t *data_out, uint8_t *data_in, size_t len) {
    if (!self->has_lock) {
        return false;
    }

    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx < 0) {
        return false;
    }

    size_t copy_len = (len > SPI_BUFFER_SIZE) ? SPI_BUFFER_SIZE : len;

    // Try peripheral hook first
    int peripheral_result = spi_peripheral_transfer(bus_idx, data_out, data_in, copy_len);

    if (peripheral_result == 0) {
        // Peripheral hook successfully handled the transfer
        // Data is already in data_in buffer
    } else if (peripheral_result < 0) {
        // No peripheral or legacy mode - use state array
        memcpy(data_in, spi_buses[bus_idx].last_read_data, copy_len);
    } else {
        // Peripheral returned an error - still copy state array data as fallback
        memcpy(data_in, spi_buses[bus_idx].last_read_data, copy_len);
    }

    // Store write data for JavaScript access
    memcpy(spi_buses[bus_idx].last_write_data, data_out, copy_len);
    spi_buses[bus_idx].last_write_len = copy_len;
    spi_buses[bus_idx].last_read_len = copy_len;

    // Rich path: Sync to JsProxy (triggers automatic transaction events)
    spi_sync_transaction_to_js(spi_buses[bus_idx].js_spi, data_out, copy_len, data_in, copy_len, "transfer");

    return true;
}

uint32_t common_hal_busio_spi_get_frequency(busio_spi_obj_t *self) {
    return self->baudrate;
}

uint8_t common_hal_busio_spi_get_phase(busio_spi_obj_t *self) {
    return self->phase;
}

uint8_t common_hal_busio_spi_get_polarity(busio_spi_obj_t *self) {
    return self->polarity;
}

void common_hal_busio_spi_never_reset(busio_spi_obj_t *self) {
    // Mark this SPI bus as never_reset so it persists across soft resets
    // This is important for displayio SPI displays and external flash
    uint8_t clock_pin = self->clock->number;
    uint8_t mosi_pin = (self->MOSI != NULL) ? self->MOSI->number : 0xFF;
    uint8_t miso_pin = (self->MISO != NULL) ? self->MISO->number : 0xFF;

    int8_t bus_idx = find_spi_bus(clock_pin, mosi_pin, miso_pin);
    if (bus_idx >= 0) {
        spi_buses[bus_idx].never_reset = true;

        // Also mark the pins as never_reset
        never_reset_pin_number(clock_pin);
        if (self->MOSI != NULL) {
            never_reset_pin_number(mosi_pin);
        }
        if (self->MISO != NULL) {
            never_reset_pin_number(miso_pin);
        }
    }
}
