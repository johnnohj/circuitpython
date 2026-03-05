// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2018 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/runtime.h"
#include "shared-bindings/_bleio/__init__.h"
#include "shared-bindings/_bleio/Adapter.h"
#include "common-hal/_bleio/Adapter.h"
#include "supervisor/shared/bluetooth/bluetooth.h"

// The singleton _bleio.Adapter object
bleio_adapter_obj_t common_hal_bleio_adapter_obj;

void common_hal_bleio_init(void) {
    common_hal_bleio_adapter_obj.base.type = &bleio_adapter_type;
    bleio_adapter_reset(&common_hal_bleio_adapter_obj);
}

void bleio_user_reset(void) {
    if (common_hal_bleio_adapter_get_enabled(&common_hal_bleio_adapter_obj)) {
        // Stop any user scanning or advertising.
        common_hal_bleio_adapter_stop_scan(&common_hal_bleio_adapter_obj);
        common_hal_bleio_adapter_stop_advertising(&common_hal_bleio_adapter_obj);
    }

    // Maybe start advertising the BLE workflow.
    supervisor_bluetooth_background();
}

void bleio_reset(void) {
    common_hal_bleio_adapter_obj.base.type = &bleio_adapter_type;
    if (!common_hal_bleio_adapter_get_enabled(&common_hal_bleio_adapter_obj)) {
        return;
    }

    supervisor_stop_bluetooth();
    bleio_adapter_reset(&common_hal_bleio_adapter_obj);
    common_hal_bleio_adapter_set_enabled(&common_hal_bleio_adapter_obj, false);
    supervisor_start_bluetooth();
}

void common_hal_bleio_gc_collect(void) {
    bleio_adapter_gc_collect(&common_hal_bleio_adapter_obj);
}

void common_hal_bleio_device_discover_remote_services(mp_obj_t device, mp_obj_t service_uuids_whitelist) {
    mp_raise_NotImplementedError(NULL);
}
