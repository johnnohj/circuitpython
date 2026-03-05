// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/runtime.h"
#include "shared-bindings/_bleio/Service.h"
#include "shared-bindings/_bleio/Characteristic.h"

uint32_t _common_hal_bleio_service_construct(bleio_service_obj_t *self, bleio_uuid_obj_t *uuid, bool is_secondary, mp_obj_list_t *characteristic_list) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_service_construct(bleio_service_obj_t *self, bleio_uuid_obj_t *uuid, bool is_secondary) {
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_service_deinit(bleio_service_obj_t *self) {
    // Nothing to do
}

void common_hal_bleio_service_from_remote_service(bleio_service_obj_t *self, bleio_connection_obj_t *connection, bleio_uuid_obj_t *uuid, bool is_secondary) {
    mp_raise_NotImplementedError(NULL);
}

bleio_uuid_obj_t *common_hal_bleio_service_get_uuid(bleio_service_obj_t *self) {
    return self->uuid;
}

mp_obj_tuple_t *common_hal_bleio_service_get_characteristics(bleio_service_obj_t *self) {
    return mp_obj_new_tuple(self->characteristic_list->len, self->characteristic_list->items);
}

bool common_hal_bleio_service_get_is_remote(bleio_service_obj_t *self) {
    return self->is_remote;
}

bool common_hal_bleio_service_get_is_secondary(bleio_service_obj_t *self) {
    return self->is_secondary;
}

void common_hal_bleio_service_add_characteristic(bleio_service_obj_t *self, bleio_characteristic_obj_t *characteristic, mp_buffer_info_t *initial_value_bufinfo, const char *user_description) {
    mp_raise_NotImplementedError(NULL);
}
