// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"
#include "py/objlist.h"
#include "shared-bindings/_bleio/Attribute.h"
#include "shared-module/_bleio/Characteristic.h"
#include "common-hal/_bleio/Descriptor.h"
#include "common-hal/_bleio/Service.h"
#include "common-hal/_bleio/UUID.h"

typedef struct _bleio_characteristic_obj {
    mp_obj_base_t base;
    bleio_service_obj_t *service;
    bleio_uuid_obj_t *uuid;
    mp_obj_t observer;
    uint8_t *current_value;
    uint16_t current_value_len;
    uint16_t current_value_alloc;
    uint16_t max_length;
    uint16_t def_handle;
    uint16_t handle;
    bleio_characteristic_properties_t props;
    bleio_attribute_security_mode_t read_perm;
    bleio_attribute_security_mode_t write_perm;
    mp_obj_list_t *descriptor_list;
    uint16_t user_desc_handle;
    uint16_t cccd_handle;
    uint16_t sccd_handle;
    bool fixed_length;
} bleio_characteristic_obj_t;

void bleio_characteristic_set_observer(bleio_characteristic_obj_t *self, mp_obj_t observer);
void bleio_characteristic_clear_observer(bleio_characteristic_obj_t *self);
