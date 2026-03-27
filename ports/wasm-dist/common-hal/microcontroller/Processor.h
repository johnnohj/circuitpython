// WASI port — no real processor, minimal stub for circuitpy_mpconfig.h
#pragma once

#include "py/obj.h"

#define COMMON_HAL_MCU_PROCESSOR_UID_LENGTH 16

typedef struct {
    mp_obj_base_t base;
} mcu_processor_obj_t;
