// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - virtual pins for JavaScript simulation

#pragma once

#include <assert.h>
#include <stdint.h>

#include <py/obj.h>

// Pin capability flags (can be OR'd together)
#define CAP_NONE     0x00
#define CAP_GPIO     0x01  // Digital I/O
#define CAP_ADC      0x02  // Analog input
#define CAP_DAC      0x04  // Analog output
#define CAP_PWM      0x08  // PWM output
#define CAP_I2C      0x10  // I2C (SDA/SCL)
#define CAP_SPI      0x20  // SPI (MOSI/MISO/SCK)
#define CAP_UART     0x40  // UART (TX/RX)
#define CAP_SPECIAL  0x80  // Special function (LED, NEOPIXEL, etc.)

typedef struct {
    mp_obj_base_t base;
    uint8_t number;         // Global pin number (0-63)
    bool enabled;           // Set by supervisor based on profile
    uint8_t capabilities;   // OR'd CAP_* flags
    bool claimed;           // In use by a module
    bool never_reset;       // Don't reset on soft reload
} mcu_pin_obj_t;

// ============================================================================
// GPIO PINS (GPIO0-63) - Universal naming for all board types
// ============================================================================
// Organized into 4 banks of 16 pins for granular profile control:
// - Bank 0: GPIO0-15   (RP2040 low, ESP32 low, SAMD mapped)
// - Bank 1: GPIO16-31  (RP2040 high, ESP32 mid, SAMD mapped)
// - Bank 2: GPIO32-47  (ESP32 high, unused for RP2040/SAMD)
// - Bank 3: GPIO48-63  (ESP32 extended, reserved for future)

// Bank 0 (GPIO0-15)
extern mcu_pin_obj_t pin_GPIO0;
extern mcu_pin_obj_t pin_GPIO1;
extern mcu_pin_obj_t pin_GPIO2;
extern mcu_pin_obj_t pin_GPIO3;
extern mcu_pin_obj_t pin_GPIO4;
extern mcu_pin_obj_t pin_GPIO5;
extern mcu_pin_obj_t pin_GPIO6;
extern mcu_pin_obj_t pin_GPIO7;
extern mcu_pin_obj_t pin_GPIO8;
extern mcu_pin_obj_t pin_GPIO9;
extern mcu_pin_obj_t pin_GPIO10;
extern mcu_pin_obj_t pin_GPIO11;
extern mcu_pin_obj_t pin_GPIO12;
extern mcu_pin_obj_t pin_GPIO13;
extern mcu_pin_obj_t pin_GPIO14;
extern mcu_pin_obj_t pin_GPIO15;

// Bank 1 (GPIO16-31)
extern mcu_pin_obj_t pin_GPIO16;
extern mcu_pin_obj_t pin_GPIO17;
extern mcu_pin_obj_t pin_GPIO18;
extern mcu_pin_obj_t pin_GPIO19;
extern mcu_pin_obj_t pin_GPIO20;
extern mcu_pin_obj_t pin_GPIO21;
extern mcu_pin_obj_t pin_GPIO22;
extern mcu_pin_obj_t pin_GPIO23;
extern mcu_pin_obj_t pin_GPIO24;
extern mcu_pin_obj_t pin_GPIO25;
extern mcu_pin_obj_t pin_GPIO26;
extern mcu_pin_obj_t pin_GPIO27;
extern mcu_pin_obj_t pin_GPIO28;
extern mcu_pin_obj_t pin_GPIO29;
extern mcu_pin_obj_t pin_GPIO30;
extern mcu_pin_obj_t pin_GPIO31;

// Bank 2 (GPIO32-47)
extern mcu_pin_obj_t pin_GPIO32;
extern mcu_pin_obj_t pin_GPIO33;
extern mcu_pin_obj_t pin_GPIO34;
extern mcu_pin_obj_t pin_GPIO35;
extern mcu_pin_obj_t pin_GPIO36;
extern mcu_pin_obj_t pin_GPIO37;
extern mcu_pin_obj_t pin_GPIO38;
extern mcu_pin_obj_t pin_GPIO39;
extern mcu_pin_obj_t pin_GPIO40;
extern mcu_pin_obj_t pin_GPIO41;
extern mcu_pin_obj_t pin_GPIO42;
extern mcu_pin_obj_t pin_GPIO43;
extern mcu_pin_obj_t pin_GPIO44;
extern mcu_pin_obj_t pin_GPIO45;
extern mcu_pin_obj_t pin_GPIO46;
extern mcu_pin_obj_t pin_GPIO47;

// Bank 3 (GPIO48-63)
extern mcu_pin_obj_t pin_GPIO48;
extern mcu_pin_obj_t pin_GPIO49;
extern mcu_pin_obj_t pin_GPIO50;
extern mcu_pin_obj_t pin_GPIO51;
extern mcu_pin_obj_t pin_GPIO52;
extern mcu_pin_obj_t pin_GPIO53;
extern mcu_pin_obj_t pin_GPIO54;
extern mcu_pin_obj_t pin_GPIO55;
extern mcu_pin_obj_t pin_GPIO56;
extern mcu_pin_obj_t pin_GPIO57;
extern mcu_pin_obj_t pin_GPIO58;
extern mcu_pin_obj_t pin_GPIO59;
extern mcu_pin_obj_t pin_GPIO60;
extern mcu_pin_obj_t pin_GPIO61;
extern mcu_pin_obj_t pin_GPIO62;
extern mcu_pin_obj_t pin_GPIO63;

// ============================================================================
// Helper Macros and Functions
// ============================================================================

#define NO_PIN (&pin_GPIO0)

// Get pin by global number (0-63)
mcu_pin_obj_t *get_pin_by_number(uint8_t pin_number);

// Bank enablement functions (called by supervisor)
// Each bank controls 16 pins for granular profile management
void enable_gpio_bank_0(bool enable);   // GPIO0-15
void enable_gpio_bank_1(bool enable);   // GPIO16-31
void enable_gpio_bank_2(bool enable);   // GPIO32-47
void enable_gpio_bank_3(bool enable);   // GPIO48-63

// Pin management
void reset_all_pins(void);
void reset_pin_number(uint8_t pin_number);
void never_reset_pin_number(uint8_t pin_number);
void claim_pin(const mcu_pin_obj_t *pin);
bool pin_number_is_free(uint8_t pin_number);
