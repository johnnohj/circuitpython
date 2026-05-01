// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/common-hal/microcontroller/Processor.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// Processor.c — Virtual processor for WASM port.
//
// get/set_frequency are functional for prototyping: set_frequency
// stores the value, get_frequency returns it with a note on first
// access that this is simulated.

#include "common-hal/microcontroller/Processor.h"
#include "shared-bindings/microcontroller/Processor.h"
#include "shared-bindings/microcontroller/ResetReason.h"
#include "py/runtime.h"

#include "port/ffi_imports.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t _cpu_frequency = 100000000;  // 100 MHz default
static bool _freq_note_shown = false;

float common_hal_mcu_processor_get_temperature(void) {
    int32_t milli_c = port_get_cpu_temperature();
    if (milli_c == INT32_MIN) {
        return (float)NAN;
    }
    return (float)milli_c / 1000.0f;
}

float common_hal_mcu_processor_get_voltage(void) {
    int32_t mv = port_get_cpu_voltage();
    if (mv == 0) {
        return (float)NAN;
    }
    return (float)mv / 1000.0f;
}

uint32_t common_hal_mcu_processor_get_frequency(void) {
    if (!_freq_note_shown) {
        _freq_note_shown = true;
        fprintf(stderr, "[wasm] cpu.frequency is simulated (%u Hz)\n",
                (unsigned)_cpu_frequency);
    }
    return _cpu_frequency;
}

void common_hal_mcu_processor_set_frequency(mcu_processor_obj_t *self,
    uint32_t frequency) {
    _cpu_frequency = frequency;
    _freq_note_shown = false;  // show note again on next get
}

void common_hal_mcu_processor_get_uid(uint8_t raw_id[]) {
    raw_id[0] = 0xAF;
}

mcu_reset_reason_t common_hal_mcu_processor_get_reset_reason(void) {
    return RESET_REASON_POWER_ON;
}
