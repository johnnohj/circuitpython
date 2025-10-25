// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Shared memory region for virtual clock hardware (timing)
// NOTE: This is separate from virtual_hardware.c which handles GPIO/analog I/O

#include "shared_memory.h"
#include <emscripten.h>
#include <string.h>

// Global instance accessible to both WASM and JavaScript
// Emscripten will place this in linear memory at a known address
EMSCRIPTEN_KEEPALIVE volatile virtual_clock_hw_t virtual_clock_hw = {
    .ticks_32khz = 0,
    .cpu_frequency_hz = 120000000,  // Default 120 MHz
    .time_mode = TIME_MODE_REALTIME,
    .wasm_yields_count = 0,
    .js_ticks_count = 0,
};

// JavaScript can get the address of virtual clock hardware via this function
EMSCRIPTEN_KEEPALIVE
void* get_virtual_clock_hw_ptr(void) {
    return (void*)&virtual_clock_hw;
}

// Helper for WASM to read ticks (fast, no message queue)
uint64_t read_virtual_ticks_32khz(void) {
    return virtual_clock_hw.ticks_32khz;
}

// Helper to check timing mode
uint8_t get_time_mode(void) {
    return virtual_clock_hw.time_mode;
}
