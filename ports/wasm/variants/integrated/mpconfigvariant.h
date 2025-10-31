/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * SPDX-License-Identifier: MIT
 */

// ============================================================================
// INTEGRATED VARIANT CONFIGURATION
// ============================================================================
//
// This variant provides cooperative yielding using JavaScript-side code
// transformation (exception-based approach).
//
// Key features:
// - cooperative_supervisor.js analyzes and instruments code
// - Inserts yield checks at loop headers
// - Uses StopIteration("__YIELD__") to signal yield points
// - JavaScript catches exception and re-executes code
//
// How it works:
// - Detect loops via code_analysis.c
// - Insert: if __yield_counter__ >= 100: raise StopIteration("__YIELD__")
// - Catch exception in JS, yield to event loop
// - Re-run code (globals persist, but loop state resets)
//
// Tradeoffs:
// - Smaller binary size (no ASYNCIFY overhead)
// - Works with simple top-level loops
// - Does NOT preserve: loop iterators, generator state, nested loops
// - Good for: Basic infinite loops, simple use cases
//
// For full state preservation (generators, nested loops), use "asyncified".
// For no yielding (lightest build), use "standard" variant.
// ============================================================================

// Set base feature level
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// WASM has no physical status LEDs, disable status bar features
#define CIRCUITPY_STATUS_BAR 0

