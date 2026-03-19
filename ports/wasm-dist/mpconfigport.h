/*
 * WASM-dist port configuration
 *
 * JavaScript/Emscripten is the host OS; Python is the guest process.
 * ITCM code (VM dispatch, GC, pystack alloc) stays in the WASM binary.
 * DTCM data (mp_state_ctx, GC heap, pystack) is checkpointed to MEMFS /mem/.
 */
#pragma once

// Include variant config first — it may set MICROPY_VARIANT_MINIMAL
// which gates whether circuitpy_mpconfig.h is included below.
#include "mpconfigvariant.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <alloca.h>

/* ITCM/DTCM adaptation for WASM (see linker.h) */
#include "linker.h"

/* Variant-specific defines (e.g. MICROPY_VARIANT_ENABLE_JS_HOOK) */
#include "mpconfigvariant.h"

/* Use CircuitPython extra-features config level for banner / type defs */
#ifndef MICROPY_CONFIG_ROM_LEVEL
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#endif

/* ---- Core VM features ---- */
#define MICROPY_STACKLESS               (1)
#define MICROPY_STACKLESS_STRICT        (1)
#define MICROPY_ENABLE_PYSTACK          (1)
#define MICROPY_ENABLE_GC               (1)
#define MICROPY_ENABLE_SCHEDULER        (1)
#define MICROPY_ENABLE_VM_ABORT         (1)
#define MICROPY_ENABLE_FINALIZER        (1)
#define MICROPY_KBD_EXCEPTION           (1)

/* ---- Native code emitter: WASM backend ---- */
/* Enables @micropython.native and @micropython.viper to compile directly
 * to WebAssembly bytecodes instead of being interpreted. The JS-side
 * loader (library_asmwasm.js) wraps the emitted bytes into a valid WASM
 * module and instantiates it via WebAssembly.compile().
 *
 * setjmp is called directly (not through mp_fun_table) because Emscripten's
 * SUPPORT_LONGJMP=wasm requires static visibility of setjmp calls.
 *
 * DISABLED pending asmwasm.c structured control flow fix: the emitter
 * produces flat br/br_if without enclosing block/loop instructions.
 * The JS runtime (library_asmwasm.js) is ready — the emitter needs to
 * translate label-based jumps to WASM's structured block nesting. */
// #define MICROPY_EMIT_WASM            (1)

/* ---- Step-wise VM execution (libpyvm) ---- */
/* When enabled, the VM dispatch loop checks mp_vm_should_yield() at every
 * branch point.  When it returns true, the VM saves state into the
 * heap-allocated code_state and returns MP_VM_RETURN_YIELD.  JS drives
 * execution by calling mp_vm_step() in a loop, doing background work
 * (register sync, bc_out drain, event loop yield) between steps. */
#define MICROPY_VM_YIELD_ENABLED        (1)
/* Declared in libpyvm.js (Emscripten --js-library); checks step budget */
extern int mp_vm_should_yield(void);
/* Saved by the yield handler in vm.c so mp_vm_step() can resume at the
 * innermost frame, not the outermost.  Set by MICROPY_VM_YIELD_SAVE_STATE.
 * Uses void* because mpconfigport.h is included before mp_code_state_t
 * is defined; main.c casts back to mp_code_state_t*. */
extern void *mp_vm_yield_state;
#define MICROPY_VM_YIELD_SAVE_STATE(cs) do { mp_vm_yield_state = (void *)(cs); } while (0)

/* ---- Persistent code (needed for sys.settrace and compiler worker) ---- */
#define MICROPY_PERSISTENT_CODE_SAVE      (1)
#define MICROPY_PERSISTENT_CODE_SAVE_FILE (1)   /* mp_raw_code_save_file() → VFS */
#define MICROPY_PERSISTENT_CODE_LOAD      (1)
#define MICROPY_PY_SYS_SETTRACE           (1)

/* ---- VFS via POSIX (Emscripten FS routes to MEMFS) ---- */
#define MICROPY_VFS                     (1)
#define MICROPY_VFS_POSIX               (1)
#ifndef MICROPY_READER_VFS
#define MICROPY_READER_VFS              (1)
#endif

/* ---- Threading: _thread module maps to WebWorkers ---- */
#define MICROPY_PY_THREAD               (1)
#define MICROPY_PY_THREAD_GIL           (0)  /* no GIL: each worker is a separate WASM instance */
#define MICROPY_MPTHREADPORT_H          "mpthreadport.h"

/* ---- I/O ---- */
#define MICROPY_USE_INTERNAL_PRINTF     (0)
#define MICROPY_USE_INTERNAL_ERRNO      (1)

/* ---- Disable features not needed for worker mode ---- */
#define MICROPY_REPL_EVENT_DRIVEN       (0)
#define MICROPY_PY_MICROPYTHON_RINGIO   (0)
#define MICROPY_PY_UCTYPES              (0)
#define MICROPY_COMP_ALLOW_TOP_LEVEL_AWAIT (1)
#define MICROPY_LONGINT_IMPL            (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL              (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_EPOCH_IS_1970           (1)
#define MICROPY_PY_SYS_PLATFORM         "webassembly"

/* ---- Platform identification ---- */
#define MICROPY_HW_BOARD_NAME           "WASM-dist"
#define MICROPY_HW_MCU_NAME             "Emscripten"

/* ---- VM hook: check abort + drain /dev/bc_out every 64 bytecodes ----
 * mp_js_hook() is the ITCM equivalent: the JS engine can tier-compile it.
 * This replaces the Asyncify-based hook from the asyncified variant.    */
#if MICROPY_VARIANT_ENABLE_JS_HOOK
#define MICROPY_VM_HOOK_COUNT           (64)
#define MICROPY_VM_HOOK_INIT            static uint _hook_n = MICROPY_VM_HOOK_COUNT;
/* mp_hal_hook() is a C function in mphalport.c:
 *   - checks run deadline (C-side variable) → mp_sched_keyboard_interrupt() if elapsed
 *   - calls mp_js_hook() for JS-side duties (bc_out drain)
 * This avoids any re-entrant WASM call from within the JS hook.             */
#define MICROPY_VM_HOOK_POLL            \
    if (--_hook_n == 0) {               \
        _hook_n = MICROPY_VM_HOOK_COUNT; \
        extern void mp_hal_hook(void);  \
        mp_hal_hook();                  \
    }
#define MICROPY_VM_HOOK_LOOP            MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN          MICROPY_VM_HOOK_POLL
#endif

/* ---- Event poll (scheduler) ---- */
#define MICROPY_EVENT_POLL_HOOK \
    do { \
        extern void mp_handle_pending(bool); \
        mp_handle_pending(true); \
    } while (0);

/* ---- Port state ---- */
#define MP_STATE_PORT MP_STATE_VM

/* ---- Random seed from JS ---- */
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (mp_js_random_u32())

/* ---- GNU extensions for dirent.h DT_xxx when VFS enabled ---- */
#if MICROPY_VFS
#define _GNU_SOURCE
#endif

/* ---- Type definitions (32-bit WASM) ---- */
typedef int mp_int_t;
typedef unsigned int mp_uint_t;
typedef long mp_off_t;
#define MP_SSIZE_MAX (0x7fffffff)

/* ---- JS host function declarations ---- */
extern void     mp_js_hook(void);
extern int      mp_js_ticks_ms(void);
extern double   mp_js_time_ms(void);
extern uint32_t mp_js_random_u32(void);
extern void     mp_js_write(const char *str, mp_uint_t len);
extern void     mp_js_set_deadline(uint32_t deadline_ms);

/* ---- stderr print (used by mphalport) ---- */
extern const struct _mp_print_t mp_stderr_print;

/* ---- Background tasks: run the same hook as the VM poll ----
 * Real CircuitPython routes this to background_callback_run_all().
 * We route it to mp_hal_hook() which handles:
 *   1. deadline/interrupt check
 *   2. hardware register sync from bc_in
 *   3. bc_out drain via mp_js_hook()
 * This ensures any code calling RUN_BACKGROUND_TASKS (pyexec, readline,
 * mp_hal_delay_ms, etc.) services hardware I/O and events.              */
#define RUN_BACKGROUND_TASKS do { extern void mp_hal_hook(void); mp_hal_hook(); } while (0)

/* ---- Include CircuitPython base config for type defs and REPL banner ---- */
#if !MICROPY_VARIANT_MINIMAL
#include "py/circuitpy_mpconfig.h"
#endif

/* ---- Post-circuitpy_mpconfig overrides ---- */
#ifdef MICROPY_REPL_EVENT_DRIVEN
#undef MICROPY_REPL_EVENT_DRIVEN
#endif
#define MICROPY_REPL_EVENT_DRIVEN       (0)

/* Enable GC alloc threshold for split-heap auto mode */
#ifdef MICROPY_GC_ALLOC_THRESHOLD
#undef MICROPY_GC_ALLOC_THRESHOLD
#endif
#define MICROPY_GC_ALLOC_THRESHOLD      (1)
