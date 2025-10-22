// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port has no hardware pins

#include "common-hal/microcontroller/Pin.h"

const mcu_pin_obj_t pin_PA00 = { .dummy = 0 };
