// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/microcontroller/Processor.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT

// WASM port — no real processor, minimal stub for circuitpy_mpconfig.h
#pragma once

#include "py/obj.h"

#define COMMON_HAL_MCU_PROCESSOR_UID_LENGTH 16

typedef struct {
    mp_obj_base_t base;
} mcu_processor_obj_t;
