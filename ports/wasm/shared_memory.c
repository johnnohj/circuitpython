// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Shared memory region for virtual hardware

#include "shared_memory.h"
#include <emscripten.h>

// Global instance accessible to both WASM and JavaScript
// Emscripten will place this in linear memory at a known address
EMSCRIPTEN_KEEPALIVE volatile virtual_hardware_t virtual_hardware = {
    .ticks_32khz = 0,
    .cpu_frequency_hz = 120000000,  // Default 120 MHz
    .time_mode = TIME_MODE_REALTIME,
    .wasm_yields_count = 0,
    .js_ticks_count = 0,
};

// JavaScript can get the address of virtual_hardware via this function
EMSCRIPTEN_KEEPALIVE
void* get_virtual_hardware_ptr(void) {
    return (void*)&virtual_hardware;
}

// Helper for WASM to read ticks (fast, no message queue)
uint64_t read_virtual_ticks_32khz(void) {
    return virtual_hardware.ticks_32khz;
}

// Helper to check timing mode
uint8_t get_time_mode(void) {
    return virtual_hardware.time_mode;
}
