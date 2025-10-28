/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// ============================================================================
// WASM VARIANT COMMON CONFIGURATION
// ============================================================================
//
// This file previously contained port-wide MICROPY_* flags, but these have
// been consolidated into mpconfigport.h for better organization and clarity.
//
// REFACTORING NOTES (2025):
// - All port-wide MICROPY_* flags moved to mpconfigport.h (lines 47-104)
// - This follows CircuitPython best practices: port-wide settings in
//   mpconfigport.h, variant-specific settings in mpconfigvariant.h
// - Eliminates duplication (e.g., MICROPY_FLOAT_IMPL was defined twice)
// - Makes flag hierarchy clearer: Port → Variant → Board
//
// CURRENT USAGE:
// This file is included by variants/*/mpconfigvariant.h via:
//   #include "../mpconfigvariant_common.h"
//
// For now, this file is kept as a placeholder to maintain the include chain.
// In the future, variants can include mpconfigport.h directly or this file
// can be removed entirely if no common variant-specific settings are needed.
//
// ============================================================================

// No common variant-specific settings at this time.
// All port-wide settings are in mpconfigport.h.
// Variant-specific settings (like CIRCUITPY_STATUS_BAR) are in
// variants/*/mpconfigvariant.h.
