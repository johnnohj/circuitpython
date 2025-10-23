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
    // In WASM, all run modes behave the same - we just reset
    (void)runmode;
    // No-op: next reset will reinitialize everything normally
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
// All 64 GPIO pins are always available via microcontroller.pin.GPIOxx
// Organized into 4 banks of 16 pins for granular profile control:
// - Bank 0: GPIO0-15   (RP2040 low, ESP32 low, SAMD mapped)
// - Bank 1: GPIO16-31  (RP2040 high, ESP32 mid, SAMD mapped)
// - Bank 2: GPIO32-47  (ESP32 high, unused for RP2040/SAMD)
// - Bank 3: GPIO48-63  (ESP32 extended, reserved for future)
// The board module will only expose pins that are enabled by the current profile
const mp_rom_map_elem_t mcu_pin_global_dict_table[] = {
    // Bank 0 (GPIO0-15)
    { MP_ROM_QSTR(MP_QSTR_GPIO0), MP_ROM_PTR(&pin_GPIO0) },
    { MP_ROM_QSTR(MP_QSTR_GPIO1), MP_ROM_PTR(&pin_GPIO1) },
    { MP_ROM_QSTR(MP_QSTR_GPIO2), MP_ROM_PTR(&pin_GPIO2) },
    { MP_ROM_QSTR(MP_QSTR_GPIO3), MP_ROM_PTR(&pin_GPIO3) },
    { MP_ROM_QSTR(MP_QSTR_GPIO4), MP_ROM_PTR(&pin_GPIO4) },
    { MP_ROM_QSTR(MP_QSTR_GPIO5), MP_ROM_PTR(&pin_GPIO5) },
    { MP_ROM_QSTR(MP_QSTR_GPIO6), MP_ROM_PTR(&pin_GPIO6) },
    { MP_ROM_QSTR(MP_QSTR_GPIO7), MP_ROM_PTR(&pin_GPIO7) },
    { MP_ROM_QSTR(MP_QSTR_GPIO8), MP_ROM_PTR(&pin_GPIO8) },
    { MP_ROM_QSTR(MP_QSTR_GPIO9), MP_ROM_PTR(&pin_GPIO9) },
    { MP_ROM_QSTR(MP_QSTR_GPIO10), MP_ROM_PTR(&pin_GPIO10) },
    { MP_ROM_QSTR(MP_QSTR_GPIO11), MP_ROM_PTR(&pin_GPIO11) },
    { MP_ROM_QSTR(MP_QSTR_GPIO12), MP_ROM_PTR(&pin_GPIO12) },
    { MP_ROM_QSTR(MP_QSTR_GPIO13), MP_ROM_PTR(&pin_GPIO13) },
    { MP_ROM_QSTR(MP_QSTR_GPIO14), MP_ROM_PTR(&pin_GPIO14) },
    { MP_ROM_QSTR(MP_QSTR_GPIO15), MP_ROM_PTR(&pin_GPIO15) },

    // Bank 1 (GPIO16-31)
    { MP_ROM_QSTR(MP_QSTR_GPIO16), MP_ROM_PTR(&pin_GPIO16) },
    { MP_ROM_QSTR(MP_QSTR_GPIO17), MP_ROM_PTR(&pin_GPIO17) },
    { MP_ROM_QSTR(MP_QSTR_GPIO18), MP_ROM_PTR(&pin_GPIO18) },
    { MP_ROM_QSTR(MP_QSTR_GPIO19), MP_ROM_PTR(&pin_GPIO19) },
    { MP_ROM_QSTR(MP_QSTR_GPIO20), MP_ROM_PTR(&pin_GPIO20) },
    { MP_ROM_QSTR(MP_QSTR_GPIO21), MP_ROM_PTR(&pin_GPIO21) },
    { MP_ROM_QSTR(MP_QSTR_GPIO22), MP_ROM_PTR(&pin_GPIO22) },
    { MP_ROM_QSTR(MP_QSTR_GPIO23), MP_ROM_PTR(&pin_GPIO23) },
    { MP_ROM_QSTR(MP_QSTR_GPIO24), MP_ROM_PTR(&pin_GPIO24) },
    { MP_ROM_QSTR(MP_QSTR_GPIO25), MP_ROM_PTR(&pin_GPIO25) },
    { MP_ROM_QSTR(MP_QSTR_GPIO26), MP_ROM_PTR(&pin_GPIO26) },
    { MP_ROM_QSTR(MP_QSTR_GPIO27), MP_ROM_PTR(&pin_GPIO27) },
    { MP_ROM_QSTR(MP_QSTR_GPIO28), MP_ROM_PTR(&pin_GPIO28) },
    { MP_ROM_QSTR(MP_QSTR_GPIO29), MP_ROM_PTR(&pin_GPIO29) },
    { MP_ROM_QSTR(MP_QSTR_GPIO30), MP_ROM_PTR(&pin_GPIO30) },
    { MP_ROM_QSTR(MP_QSTR_GPIO31), MP_ROM_PTR(&pin_GPIO31) },

    // Bank 2 (GPIO32-47)
    { MP_ROM_QSTR(MP_QSTR_GPIO32), MP_ROM_PTR(&pin_GPIO32) },
    { MP_ROM_QSTR(MP_QSTR_GPIO33), MP_ROM_PTR(&pin_GPIO33) },
    { MP_ROM_QSTR(MP_QSTR_GPIO34), MP_ROM_PTR(&pin_GPIO34) },
    { MP_ROM_QSTR(MP_QSTR_GPIO35), MP_ROM_PTR(&pin_GPIO35) },
    { MP_ROM_QSTR(MP_QSTR_GPIO36), MP_ROM_PTR(&pin_GPIO36) },
    { MP_ROM_QSTR(MP_QSTR_GPIO37), MP_ROM_PTR(&pin_GPIO37) },
    { MP_ROM_QSTR(MP_QSTR_GPIO38), MP_ROM_PTR(&pin_GPIO38) },
    { MP_ROM_QSTR(MP_QSTR_GPIO39), MP_ROM_PTR(&pin_GPIO39) },
    { MP_ROM_QSTR(MP_QSTR_GPIO40), MP_ROM_PTR(&pin_GPIO40) },
    { MP_ROM_QSTR(MP_QSTR_GPIO41), MP_ROM_PTR(&pin_GPIO41) },
    { MP_ROM_QSTR(MP_QSTR_GPIO42), MP_ROM_PTR(&pin_GPIO42) },
    { MP_ROM_QSTR(MP_QSTR_GPIO43), MP_ROM_PTR(&pin_GPIO43) },
    { MP_ROM_QSTR(MP_QSTR_GPIO44), MP_ROM_PTR(&pin_GPIO44) },
    { MP_ROM_QSTR(MP_QSTR_GPIO45), MP_ROM_PTR(&pin_GPIO45) },
    { MP_ROM_QSTR(MP_QSTR_GPIO46), MP_ROM_PTR(&pin_GPIO46) },
    { MP_ROM_QSTR(MP_QSTR_GPIO47), MP_ROM_PTR(&pin_GPIO47) },

    // Bank 3 (GPIO48-63)
    { MP_ROM_QSTR(MP_QSTR_GPIO48), MP_ROM_PTR(&pin_GPIO48) },
    { MP_ROM_QSTR(MP_QSTR_GPIO49), MP_ROM_PTR(&pin_GPIO49) },
    { MP_ROM_QSTR(MP_QSTR_GPIO50), MP_ROM_PTR(&pin_GPIO50) },
    { MP_ROM_QSTR(MP_QSTR_GPIO51), MP_ROM_PTR(&pin_GPIO51) },
    { MP_ROM_QSTR(MP_QSTR_GPIO52), MP_ROM_PTR(&pin_GPIO52) },
    { MP_ROM_QSTR(MP_QSTR_GPIO53), MP_ROM_PTR(&pin_GPIO53) },
    { MP_ROM_QSTR(MP_QSTR_GPIO54), MP_ROM_PTR(&pin_GPIO54) },
    { MP_ROM_QSTR(MP_QSTR_GPIO55), MP_ROM_PTR(&pin_GPIO55) },
    { MP_ROM_QSTR(MP_QSTR_GPIO56), MP_ROM_PTR(&pin_GPIO56) },
    { MP_ROM_QSTR(MP_QSTR_GPIO57), MP_ROM_PTR(&pin_GPIO57) },
    { MP_ROM_QSTR(MP_QSTR_GPIO58), MP_ROM_PTR(&pin_GPIO58) },
    { MP_ROM_QSTR(MP_QSTR_GPIO59), MP_ROM_PTR(&pin_GPIO59) },
    { MP_ROM_QSTR(MP_QSTR_GPIO60), MP_ROM_PTR(&pin_GPIO60) },
    { MP_ROM_QSTR(MP_QSTR_GPIO61), MP_ROM_PTR(&pin_GPIO61) },
    { MP_ROM_QSTR(MP_QSTR_GPIO62), MP_ROM_PTR(&pin_GPIO62) },
    { MP_ROM_QSTR(MP_QSTR_GPIO63), MP_ROM_PTR(&pin_GPIO63) },
};
MP_DEFINE_CONST_DICT(mcu_pin_globals, mcu_pin_global_dict_table);
