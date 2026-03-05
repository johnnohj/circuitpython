// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/mperrno.h"
#include "py/runtime.h"
#include "shared-bindings/_bleio/CharacteristicBuffer.h"

void _common_hal_bleio_characteristic_buffer_construct(bleio_characteristic_buffer_obj_t *self,
    bleio_characteristic_obj_t *characteristic,
    mp_float_t timeout,
    uint8_t *buffer, size_t buffer_size,
    void *static_handler_entry,
    bool watch_for_interrupt_char) {
    (void)self;
    (void)characteristic;
    (void)timeout;
    (void)buffer;
    (void)buffer_size;
    (void)static_handler_entry;
    (void)watch_for_interrupt_char;
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_characteristic_buffer_construct(bleio_characteristic_buffer_obj_t *self,
    bleio_characteristic_obj_t *characteristic,
    mp_float_t timeout,
    size_t buffer_size) {
    (void)self;
    (void)characteristic;
    (void)timeout;
    (void)buffer_size;
    mp_raise_NotImplementedError(NULL);
}

uint32_t common_hal_bleio_characteristic_buffer_read(bleio_characteristic_buffer_obj_t *self, uint8_t *data, size_t len, int *errcode) {
    (void)self;
    (void)data;
    (void)len;
    if (errcode != NULL) {
        *errcode = MP_EAGAIN;
    }
    mp_raise_NotImplementedError(NULL);
}

uint32_t common_hal_bleio_characteristic_buffer_rx_characters_available(bleio_characteristic_buffer_obj_t *self) {
    (void)self;
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_characteristic_buffer_clear_rx_buffer(bleio_characteristic_buffer_obj_t *self) {
    (void)self;
    mp_raise_NotImplementedError(NULL);
}

bool common_hal_bleio_characteristic_buffer_deinited(bleio_characteristic_buffer_obj_t *self) {
    return self->deinited;
}

void common_hal_bleio_characteristic_buffer_deinit(bleio_characteristic_buffer_obj_t *self) {
    if (self == NULL) {
        return;
    }
    self->deinited = true;
}

bool common_hal_bleio_characteristic_buffer_connected(bleio_characteristic_buffer_obj_t *self) {
    (void)self;
    return false;
}
