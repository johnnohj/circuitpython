// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <string.h>

#include "py/runtime.h"
#include "shared-bindings/_bleio/UUID.h"

void common_hal_bleio_uuid_construct(bleio_uuid_obj_t *self, mp_int_t uuid16, const uint8_t uuid128[16]) {
    if (uuid16 != 0) {
        // 16-bit UUID
        self->size = 16;
        // Convert 16-bit UUID to 128-bit
        // Bluetooth Base UUID: 00000000-0000-1000-8000-00805F9B34FB
        const uint8_t base_uuid[16] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        memcpy(self->uuid128, base_uuid, 16);
        self->uuid128[12] = (uuid16 & 0xff);
        self->uuid128[13] = (uuid16 >> 8) & 0xff;
    } else {
        // 128-bit UUID
        self->size = 128;
        memcpy(self->uuid128, uuid128, 16);
    }
}

uint32_t common_hal_bleio_uuid_get_uuid16(bleio_uuid_obj_t *self) {
    if (self->size == 16) {
        return (self->uuid128[13] << 8) | self->uuid128[12];
    }
    return 0;
}

void common_hal_bleio_uuid_get_uuid128(bleio_uuid_obj_t *self, uint8_t uuid128[16]) {
    memcpy(uuid128, self->uuid128, 16);
}

uint32_t common_hal_bleio_uuid_get_size(bleio_uuid_obj_t *self) {
    return self->size;
}

void common_hal_bleio_uuid_pack_into(bleio_uuid_obj_t *self, uint8_t *buf) {
    if (self->size == 16) {
        buf[0] = self->uuid128[12];
        buf[1] = self->uuid128[13];
    } else {
        memcpy(buf, self->uuid128, 16);
    }
}
