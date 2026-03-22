/*
 * WASI reactor variant — externally-driven execution.
 *
 * The JS host is the supervisor. Instead of _start running to completion,
 * the binary exports init/step functions. The host calls step() in a loop,
 * driving Python execution in budget-limited bursts.
 *
 * Requires stackless mode so Python call frames live on the heap
 * (not the C stack), allowing clean yield/resume between steps.
 */
#pragma once

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// Reactor model: yield-enabled VM with stackless execution
#define MICROPY_VM_YIELD_ENABLED    (1)
#define MICROPY_STACKLESS           (1)
#define MICROPY_STACKLESS_STRICT    (1)
#define MICROPY_ENABLE_PYSTACK      (1)

// Disable native emitters (wasm32 target)
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
