/*
 * WASI standard variant
 *
 * ROM level stays at CORE_FEATURES to avoid pulling in modules that need
 * supervisor or hardware. We explicitly enable the features CircuitPython
 * hardcodes in py/circuitpy_mpconfig.h, plus the CIRCUITPY_FULL_BUILD
 * features that are pure software (no hardware dependencies).
 */
#pragma once

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// Disable native emitters (wasm32 target)
#define MICROPY_EMIT_X86 (0)
#define MICROPY_EMIT_X64 (0)
#define MICROPY_EMIT_THUMB (0)
#define MICROPY_EMIT_ARM (0)

// Tune parser for smaller memory footprint
#define MICROPY_ALLOC_QSTR_CHUNK_INIT (64)
#define MICROPY_ALLOC_PARSE_RULE_INIT (8)
#define MICROPY_ALLOC_PARSE_RULE_INC  (8)
#define MICROPY_ALLOC_PARSE_RESULT_INIT (8)
#define MICROPY_ALLOC_PARSE_RESULT_INC (8)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (64)

// ---- Features CircuitPython hardcodes ON (circuitpy_mpconfig.h) ----
// These are always-on regardless of CIRCUITPY_FULL_BUILD.
#define MICROPY_PY_BUILTINS_SLICE_ATTRS     (1)
#define MICROPY_PY_BUILTINS_SLICE_INDICES   (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW      (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE     (1)
#define MICROPY_PY_COLLECTIONS              (1)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT  (1)
#define MICROPY_PY_DESCRIPTORS              (1)
#define MICROPY_PY_SYS_STDFILES             (1)
#define MICROPY_PY_JSON                     (1)

// ---- CIRCUITPY_FULL_BUILD features (pure software, no hardware) ----
// Error reporting
#define MICROPY_ERROR_REPORTING             (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_CPYTHON_COMPAT             (1)
#define MICROPY_CPYTHON_EXCEPTION_CHAIN    (1)
#define MICROPY_BUILTIN_METHOD_CHECK_SELF_ARG (1)

// Python language completeness
#define MICROPY_PY_ALL_SPECIAL_METHODS     (1)
#define MICROPY_PY_REVERSE_SPECIAL_METHODS (1)
#define MICROPY_PY_BUILTINS_COMPLEX        (1)
#define MICROPY_PY_BUILTINS_FROZENSET      (1)
#define MICROPY_PY_BUILTINS_NOTIMPLEMENTED (1)
#define MICROPY_PY_BUILTINS_STR_CENTER     (1)
#define MICROPY_PY_BUILTINS_STR_PARTITION  (1)
#define MICROPY_PY_BUILTINS_STR_SPLITLINES (1)
#define MICROPY_PY_FUNCTION_ATTRS          (1)
#define MICROPY_PY_DOUBLE_TYPECODE         (1)
#define MICROPY_PY_BUILTINS_POW3           (1)

// Collections
#define MICROPY_PY_COLLECTIONS_DEQUE       (1)
#define MICROPY_PY_COLLECTIONS_DEQUE_ITER  (1)
#define MICROPY_PY_COLLECTIONS_DEQUE_SUBSCR (1)

// ---- Features for a useful REPL ----
#define MICROPY_COMP_CONST_FOLDING (1)
#define MICROPY_COMP_CONST_LITERAL (1)
#define MICROPY_COMP_CONST_TUPLE (1)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN (1)
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_FULL_CHECKS (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_KBD_EXCEPTION (1)
#define MICROPY_MODULE_GETATTR (1)
#define MICROPY_MULTIPLE_INHERITANCE (1)
#define MICROPY_PY_ASSIGN_EXPR (1)
#define MICROPY_PY_ASYNC_AWAIT (1)
#define MICROPY_PY_ATTRTUPLE (1)
#define MICROPY_PY_BUILTINS_DICT_FROMKEYS (1)
#define MICROPY_PY_BUILTINS_RANGE_ATTRS (1)
#define MICROPY_PY_GENERATOR_PEND_THROW (1)

// Enable sys and os
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_SYS_ARGV (1)
#define MICROPY_PY_OS (1)

// Float support
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)

// Use MicroPython readline (not GNU readline)
#define MICROPY_USE_READLINE (1)
#define MICROPY_USE_READLINE_HISTORY (1)
