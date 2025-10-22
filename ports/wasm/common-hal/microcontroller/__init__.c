// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM port - stub microcontroller module

#include "py/mphal.h"
#include "py/obj.h"

#include "shared-bindings/microcontroller/__init__.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "shared-bindings/microcontroller/Processor.h"
#include "message_queue.h"

void common_hal_mcu_delay_us(uint32_t delay) {
    mp_hal_delay_us(delay);
}

volatile uint32_t nesting_count = 0;

void common_hal_mcu_disable_interrupts(void) {
    nesting_count++;
}

void common_hal_mcu_enable_interrupts(void) {
    if (nesting_count > 0) {
        nesting_count--;
    }
}

void common_hal_mcu_on_next_reset(mcu_runmode_t runmode) {
    // No-op for WASM
}
void common_hal_mcu_reset(void) {
    // Send reset notification to JavaScript
    int32_t req_id = message_queue_alloc();
    if (req_id >= 0) {
        message_request_t *req = message_queue_get(req_id);
        req->type = MSG_TYPE_MCU_RESET;
        message_queue_send_to_js(req_id);
        // Don't wait for response - just send the notification
        message_queue_free(req_id);
    }

    // Loop forever (function declared noreturn)
    while (1) {
        RUN_BACKGROUND_TASKS;
    }
}

// The singleton microcontroller.Processor object, bound to microcontroller.cpu
// Support for multi-processor systems (like RP2xxx dual-core)
#if CIRCUITPY_PROCESSOR_COUNT > 1
static const mcu_processor_obj_t processor0 = {
    .base = {
        .type = &mcu_processor_type,
    },
};

static const mcu_processor_obj_t processor1 = {
    .base = {
        .type = &mcu_processor_type,
    },
};

const mp_rom_obj_tuple_t common_hal_multi_processor_obj = {
    {&mp_type_tuple},
    CIRCUITPY_PROCESSOR_COUNT,
    {
        MP_ROM_PTR(&processor0),
        MP_ROM_PTR(&processor1)
    }
};
#endif

const mcu_processor_obj_t common_hal_mcu_processor_obj = {
    .base = {
        .type = &mcu_processor_type,
    },
};

// This maps MCU pin names to pin objects.
const mp_rom_map_elem_t mcu_pin_global_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_PA00), MP_ROM_PTR(&pin_PA00) },
};
MP_DEFINE_CONST_DICT(mcu_pin_globals, mcu_pin_global_dict_table);
