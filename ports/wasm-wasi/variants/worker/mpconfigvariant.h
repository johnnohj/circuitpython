/*
 * WASI worker variant — hardware peripheral controller.
 *
 * Runs in a dedicated Web Worker alongside the main-thread reactor.
 * Owns the display pipeline (C displayio/terminalio/fontio), REPL
 * terminal rendering, and peripheral state.  Communicates with the
 * reactor via OPFS endpoints.
 *
 * The worker has the full C display stack compiled in for performance
 * (framebuffer compositing, font rasterization, terminal emulation).
 * It optionally runs worker.py for orchestration and driver config.
 *
 * Unlike the reactor, runs to completion on the C stack (no yield).
 * Unlike the opfs executor, no state checkpointing (stateless restart).
 */
#pragma once

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

/* Display resolution — 480×360 gives 80×29 terminal at 6×12 font */
#define WASM_DISPLAY_WIDTH  480
#define WASM_DISPLAY_HEIGHT 360

/* C display pipeline — worker owns compositing, terminal, font rendering */
#define CIRCUITPY_DISPLAYIO     (1)
#define CIRCUITPY_FRAMEBUFFERIO (1)
#define CIRCUITPY_TERMINALIO    (1)
#define CIRCUITPY_FONTIO        (1)
#define CIRCUITPY_REPL_LOGO     (1)

/* Hardware peripherals — C common-hal reads/writes OPFS endpoints */
#define CIRCUITPY_MICROCONTROLLER (1)
#define CIRCUITPY_DIGITALIO       (1)
#define CIRCUITPY_ANALOGIO        (1)
#define CIRCUITPY_PWMIO           (1)
#define CIRCUITPY_NEOPIXEL_WRITE  (1)
#define CIRCUITPY_BOARD           (1)
#define CIRCUITPY_BUSIO           (1)

/* Stackless for heap-allocated frames (friendlier to WASM stack) */
#define MICROPY_STACKLESS           (1)
#define MICROPY_STACKLESS_STRICT    (1)
#define MICROPY_ENABLE_PYSTACK      (1)

/* VM yield — enables cooperative stepping for code.py execution.
 * The VM checks mp_vm_should_yield() at backwards branches and returns
 * MP_VM_RETURN_YIELD.  The JS host drives the step loop. */
#define MICROPY_VM_YIELD_ENABLED    (1)

/* VM hooks for background tasks (display refresh, endpoint polling) */
#define MICROPY_ENABLE_SCHEDULER    (1)

/* Disable native emitters (wasm32) */
#define MICROPY_EMIT_X86    (0)
#define MICROPY_EMIT_X64    (0)
#define MICROPY_EMIT_THUMB  (0)
#define MICROPY_EMIT_ARM    (0)

/* Parser tuning */
#define MICROPY_ALLOC_QSTR_CHUNK_INIT   (64)
#define MICROPY_ALLOC_PARSE_RULE_INIT   (8)
#define MICROPY_ALLOC_PARSE_RULE_INC    (8)
#define MICROPY_ALLOC_PARSE_RESULT_INIT (8)
#define MICROPY_ALLOC_PARSE_RESULT_INC  (8)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT  (64)

/* Language features */
#define MICROPY_COMP_CONST_FOLDING          (1)
#define MICROPY_COMP_CONST_LITERAL          (1)
#define MICROPY_COMP_CONST_TUPLE            (1)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN    (1)
#define MICROPY_ENABLE_COMPILER             (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT      (1)
#define MICROPY_FULL_CHECKS                 (1)
#define MICROPY_KBD_EXCEPTION               (1)
#define MICROPY_MODULE_GETATTR              (1)
#define MICROPY_MULTIPLE_INHERITANCE        (1)

/* Builtins needed by worker.py */
#define MICROPY_PY_SYS          (1)
#define MICROPY_PY_SYS_PATH     (1)
#define MICROPY_PY_OS           (1)
#define MICROPY_PY_STRUCT       (1)

/* Numerics */
#define MICROPY_FLOAT_IMPL      (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_LONGINT_IMPL    (MICROPY_LONGINT_IMPL_MPZ)

/* Event-driven REPL — JS host feeds one char at a time, no blocking.
 * circuitpy_mpconfig.h clobbers this to 0; _WANTED sentinel lets
 * mpconfigport.h re-apply it after the include. */
#define MICROPY_REPL_EVENT_DRIVEN (1)
#define _MICROPY_REPL_EVENT_DRIVEN_WANTED (1)
#define MICROPY_USE_READLINE      (1)
#define MICROPY_USE_READLINE_HISTORY (1)
#define MICROPY_HELPER_REPL       (1)
