// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <errno.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include "py/runtime.h"
#include "bindings/zephyr_kernel/__init__.h"
#include "shared-bindings/_bleio/__init__.h"
#include "shared-bindings/_bleio/Connection.h"

void common_hal_bleio_connection_pair(bleio_connection_internal_t *self, bool bond) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_connection_disconnect(bleio_connection_internal_t *self) {
    if (self == NULL || self->conn == NULL) {
        return;
    }

    int err = bt_conn_disconnect(self->conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (err != 0 && err != -ENOTCONN) {
        raise_zephyr_error(err);
    }

    // The connection may now be disconnecting; force connections tuple rebuild.
    common_hal_bleio_adapter_obj.connection_objs = NULL;
}

bool common_hal_bleio_connection_get_connected(bleio_connection_obj_t *self) {
    if (self == NULL || self->connection == NULL) {
        return false;
    }

    bleio_connection_internal_t *connection = self->connection;
    if (connection->conn == NULL) {
        return false;
    }

    struct bt_conn_info info;
    if (bt_conn_get_info(connection->conn, &info) != 0) {
        return false;
    }

    return info.state == BT_CONN_STATE_CONNECTED || info.state == BT_CONN_STATE_DISCONNECTING;
}

mp_int_t common_hal_bleio_connection_get_max_packet_length(bleio_connection_internal_t *self) {
    if (self == NULL || self->conn == NULL) {
        return 20;
    }

    uint16_t mtu = bt_gatt_get_mtu(self->conn);
    if (mtu < 3) {
        return 20;
    }
    return mtu - 3;
}

bool common_hal_bleio_connection_get_paired(bleio_connection_obj_t *self) {
    return false;
}

mp_obj_tuple_t *common_hal_bleio_connection_discover_remote_services(bleio_connection_obj_t *self, mp_obj_t service_uuids_whitelist) {
    mp_raise_NotImplementedError(NULL);
}

mp_float_t common_hal_bleio_connection_get_connection_interval(bleio_connection_internal_t *self) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_connection_set_connection_interval(bleio_connection_internal_t *self, mp_float_t new_interval) {
    mp_raise_NotImplementedError(NULL);
}

mp_obj_t bleio_connection_new_from_internal(bleio_connection_internal_t *connection) {
    if (connection == NULL) {
        return mp_const_none;
    }

    if (connection->connection_obj != mp_const_none) {
        return connection->connection_obj;
    }

    bleio_connection_obj_t *connection_obj = mp_obj_malloc(bleio_connection_obj_t, &bleio_connection_type);
    connection_obj->connection = connection;
    connection_obj->disconnect_reason = 0;
    connection->connection_obj = MP_OBJ_FROM_PTR(connection_obj);

    return connection->connection_obj;
}
