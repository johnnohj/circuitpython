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
    }
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

    // Update configuration
    spi_buses[bus_idx].baudrate = baudrate;
    spi_buses[bus_idx].polarity = polarity;
    spi_buses[bus_idx].phase = phase;
    spi_buses[bus_idx].bits = bits;

    // Store in object for quick access
    self->baudrate = baudrate;
    self->polarity = polarity;
    self->phase = phase;
    self->bits = bits;

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

    // Store write data for JavaScript access (up to buffer size)
    size_t copy_len = (len > SPI_BUFFER_SIZE) ? SPI_BUFFER_SIZE : len;
    memcpy(spi_buses[bus_idx].last_write_data, data, copy_len);
    spi_buses[bus_idx].last_write_len = copy_len;

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

    // For WASM simulation, JavaScript can write to last_read_data
    // to simulate peripheral responses
    size_t copy_len = (len > SPI_BUFFER_SIZE) ? SPI_BUFFER_SIZE : len;
    memcpy(data, spi_buses[bus_idx].last_read_data, copy_len);
    spi_buses[bus_idx].last_read_len = copy_len;

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

    // Store write data and read simulated response
    size_t copy_len = (len > SPI_BUFFER_SIZE) ? SPI_BUFFER_SIZE : len;
    memcpy(spi_buses[bus_idx].last_write_data, data_out, copy_len);
    memcpy(data_in, spi_buses[bus_idx].last_read_data, copy_len);
    spi_buses[bus_idx].last_write_len = copy_len;
    spi_buses[bus_idx].last_read_len = copy_len;

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
