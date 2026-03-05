// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"
#include "py/objlist.h"
#include "common-hal/_bleio/UUID.h"

typedef struct bleio_service_obj {
    mp_obj_base_t base;
    bleio_uuid_obj_t *uuid;
    mp_obj_t connection;
    mp_obj_list_t *characteristic_list;
    uint16_t start_handle;
    uint16_t end_handle;
    bool is_remote;
    bool is_secondary;
} bleio_service_obj_t;
