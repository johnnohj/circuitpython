// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port has no hardware pins

#pragma once

// Empty pin type - WASM has no pins
typedef struct {
    uint8_t dummy;
} mcu_pin_obj_t;

extern const mcu_pin_obj_t pin_PA00;

#define NO_PIN (&pin_PA00)
