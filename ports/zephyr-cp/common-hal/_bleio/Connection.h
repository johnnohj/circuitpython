// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>

#include "py/obj.h"

struct bt_conn;

typedef struct {
    struct bt_conn *conn;
    mp_obj_t connection_obj;
} bleio_connection_internal_t;

typedef struct {
    mp_obj_base_t base;
    bleio_connection_internal_t *connection;
    uint8_t disconnect_reason;
} bleio_connection_obj_t;

mp_obj_t bleio_connection_new_from_internal(bleio_connection_internal_t *connection);
