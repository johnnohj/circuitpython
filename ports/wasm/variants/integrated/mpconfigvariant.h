/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * SPDX-License-Identifier: MIT
 */

// Integrated variant - Full CircuitPython supervisor compatibility

// Set base feature level.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// Enable extra Unix features.
#include "../mpconfigvariant_common.h"
