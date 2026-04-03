/*
 * supervisor/compile.h — Unified compilation service.
 */
#pragma once

#include "py/bc.h"
#include "py/parse.h"

/* Compile source text to a ready-to-run code_state on pystack.
 * Returns NULL on error (already printed). */
mp_code_state_t *cp_compile_str(const char *src, size_t len,
                                 mp_parse_input_kind_t mode);

/* Compile a .py file to a ready-to-run code_state on pystack.
 * Returns NULL on error (already printed). */
mp_code_state_t *cp_compile_file(const char *path);
