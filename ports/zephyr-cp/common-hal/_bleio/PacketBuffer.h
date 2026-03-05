// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>

#include "py/obj.h"

typedef struct _bleio_characteristic_obj bleio_characteristic_obj_t;

typedef void *ble_event_handler_t;

typedef struct {
    mp_obj_base_t base;
    bool deinited;
} bleio_packet_buffer_obj_t;
