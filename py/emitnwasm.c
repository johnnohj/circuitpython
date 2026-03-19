// Emitter wrapper for WASM native code generation.
// This follows the same pattern as emitnthumb.c, emitnx64.c, etc.

// First, include the header that provides the GENERIC_ASM_API macros.
#include "py/mpconfig.h"

#if MICROPY_EMIT_WASM

#define GENERIC_ASM_API (1)
#define N_WASM (1)
#define EXPORT_FUN(name) emit_native_wasm_##name

// NLR buffer index where LOCAL_1 is saved during exception handling.
// WASM uses structured exception handling (WASM EH proposal), so this
// index is nominal — the actual save/restore uses WASM locals, not a
// buffer in linear memory. We use index 5 (same as x64/debug) as a
// placeholder for the NLR_BUF layout that emitnative.c expects.
#define NLR_BUF_IDX_LOCAL_1 (5)

// WASM uses setjmp-based NLR (via WASM exception handling or
// Emscripten's SUPPORT_LONGJMP=wasm). Tell emitnative.c to use
// the setjmp path for exception handling setup/teardown.
#define N_NLR_SETJMP (1)

#include "py/asmwasm.h"
#include "py/emitnative.c"

#endif
