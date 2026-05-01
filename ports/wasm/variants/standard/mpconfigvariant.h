// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/variants/standard/mpconfigvariant.h
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// Standard variant — CLI REPL, no display.
// Hardware modules (digitalio, analogio, busio, etc.) are enabled
// via mpconfigboard.h, shared with the browser variant.

// Set base feature level.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// Enable extra Unix features.
#include "../mpconfigvariant_common.h"

// Board config — pin definitions, CIRCUITPY_* hardware flags
#include "mpconfigboard.h"

// ── Abort-resume: halt/resume via nlr_jump_abort ──
#define MICROPY_ENABLE_PYSTACK      (1)
#define MICROPY_STACKLESS           (1)
#define MICROPY_STACKLESS_STRICT    (1)
#define MICROPY_ENABLE_VM_ABORT     (1)

// ── Event-driven REPL ──
// Both variants use the frame loop, so the REPL must be non-blocking.
// pyexec_event_repl_process_char() processes one character at a time.
#define MICROPY_REPL_EVENT_DRIVEN   (1)
