// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/linker.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// supervisor/linker.h — WASM port linker macros.
// On WASM, all memory is linear — ITCM/DTCM macros are identity no-ops.

#pragma once

#define PLACE_IN_ITCM(name)         name
#define PLACE_IN_DTCM_DATA(name)    name
#define PLACE_IN_DTCM_BSS(name)     name
