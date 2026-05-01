// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/compile.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// supervisor/compile.h — Unified compilation service.
//
// Design refs:
//   design/behavior/04-script-execution.md  (compile step in lifecycle)

#pragma once

#include "py/bc.h"
#include "py/parse.h"

// Compile source text to a ready-to-run code_state on pystack.
// Returns NULL on error (already printed).
mp_code_state_t *cp_compile_str(const char *src, size_t len,
                                 mp_parse_input_kind_t mode);

// Compile a .py file to a ready-to-run code_state on pystack.
// Returns NULL on error (already printed).
mp_code_state_t *cp_compile_file(const char *path);
