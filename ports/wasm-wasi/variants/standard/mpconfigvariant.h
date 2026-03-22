/*
 * WASI standard variant — simple REPL on stdin/stdout.
 *
 * Module configuration comes from circuitpy_mpconfig.h via CIRCUITPY_*
 * flags in mpconfigboard.h. This file only sets variant-specific knobs.
 */
#pragma once

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// Disable native emitters (wasm32 target — no ARM/x86 code generation)
#define MICROPY_EMIT_X86 (0)
#define MICROPY_EMIT_X64 (0)
#define MICROPY_EMIT_THUMB (0)
#define MICROPY_EMIT_ARM (0)

// Parser memory tuning
#define MICROPY_ALLOC_QSTR_CHUNK_INIT (64)
#define MICROPY_ALLOC_PARSE_RULE_INIT (8)
#define MICROPY_ALLOC_PARSE_RULE_INC  (8)
#define MICROPY_ALLOC_PARSE_RESULT_INIT (8)
#define MICROPY_ALLOC_PARSE_RESULT_INC (8)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (64)

// Float support
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)

// Use MicroPython readline (not GNU readline)
#define MICROPY_USE_READLINE (1)
#define MICROPY_USE_READLINE_HISTORY (1)
