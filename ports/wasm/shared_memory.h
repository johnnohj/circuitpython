// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// Shared memory region between WASM and JavaScript
// JavaScript writes, WASM reads (no locks needed for monotonic counters)

#pragma once

#include <stdint.h>
#include <stdbool.h>

// Memory-mapped virtual clock hardware (like Renode's 0x40001000)
// This struct is allocated by Emscripten and shared with JavaScript
// NOTE: This is separate from virtual_hardware.c which handles GPIO/analog I/O
typedef struct {
    // Virtual 32kHz crystal tick counter
    // JavaScript increments this, CircuitPython reads it
    // Simulates a real hardware crystal oscillator
    volatile uint64_t ticks_32khz;

    // CPU frequency in Hz (controlled by JavaScript)
    // Used for instruction timing and virtual execution speed
    volatile uint32_t cpu_frequency_hz;

    // Timing mode flags
    volatile uint8_t time_mode;  // 0=realtime, 1=manual, 2=fast-forward

    // Padding for alignment
    uint8_t _padding[3];

    // Statistics (read-only for WASM)
    volatile uint64_t wasm_yields_count;
    volatile uint64_t js_ticks_count;
} virtual_clock_hw_t;

// Export for JavaScript to access
// Emscripten will create this in linear memory
extern volatile virtual_clock_hw_t virtual_clock_hw;

// Timing modes
#define TIME_MODE_REALTIME     0
#define TIME_MODE_MANUAL       1  // For step-by-step debugging
#define TIME_MODE_FAST_FORWARD 2  // Skip delays instantly

// Function declarations
void* get_virtual_clock_hw_ptr(void);
uint64_t read_virtual_ticks_32khz(void);
uint8_t get_time_mode(void);
