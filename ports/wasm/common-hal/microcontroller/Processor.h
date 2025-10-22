// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - stub processor

#pragma once

#define COMMON_HAL_MCU_PROCESSOR_UID_LENGTH 1

#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    // Stores no state currently.
} mcu_processor_obj_t;
