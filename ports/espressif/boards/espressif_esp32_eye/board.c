// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2020 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "supervisor/board.h"
#include "mpconfigboard.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "driver/gpio.h"
#include "common-hal/microcontroller/Pin.h"

void board_init(void) {
    reset_board();
}

bool espressif_board_reset_pin_number(gpio_num_t pin_number) {
    // Pull LEDs down on reset rather than the default up
    if (pin_number == 21 || pin_number == 22) {
        gpio_config_t cfg = {
            .pin_bit_mask = BIT64(pin_number),
            .mode = GPIO_MODE_DISABLE,
            .pull_up_en = false,
            .pull_down_en = true,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
        return true;
    }
    return false;
}

// Use the MP_WEAK supervisor/shared/board.c versions of routines not defined here.
