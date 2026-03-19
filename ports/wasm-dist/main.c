/*
 * main.c — WASM-dist Python host interface
 *
 * JavaScript/Emscripten is the host OS; this file provides the C-side
 * interface between the JS host and the Python VM.  It is deliberately thin:
 * all scheduling, I/O routing, and state management are handled by JS
 * (library.js, PythonHost.js) and the virtual device layer (vdev.c).
 *
 * Exported functions (see Makefile EXPORTED_FUNCTIONS):
 *   mp_js_init(pystack_size, heap_size)
 *   mp_js_run(src, len, timeout_ms, out)      — new: timeout + state delta
 *   mp_js_do_exec(src, len, out)              — synchronous exec
 *   mp_js_do_import(name, out)
 *   mp_js_register_js_module(name, value)
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "shared/runtime/pyexec.h"
#include "shared/runtime/gchelper.h"
#include "py/objexcept.h"
#include "py/stream.h"

#include <fcntl.h>
#include <unistd.h>

#include "emscripten.h"
#include "lexer_dedent.h"
#include "library.h"
#include "proxy_c.h"
#include "vdev.h"
#include "memfs_state.h"

#if MICROPY_PY_SYS_SETTRACE
#include "py/profile.h"
#include "py/objcode.h"
#endif

/* ---- External call depth tracking ----
 * Tracks depth of calls from JS into C.  When it reaches 0 (top-level
 * return to JS) we can safely run a top-level GC collection.             */
static size_t external_call_depth = 0;

/* Layout matches mp_obj_vfs_posix_file_t (extmod/vfs_posix_file.c). */
typedef struct { mp_obj_base_t base; int fd; } _vfs_posix_file_t;
extern _vfs_posix_file_t mp_sys_stdout_obj;
extern _vfs_posix_file_t mp_sys_stderr_obj;

/* Printer that writes to sys.stderr (redirected to /dev/py_stderr → _pyStderr). */
static const mp_print_t mp_sys_stderr_print = {
    &mp_sys_stderr_obj, mp_stream_write_adaptor
};

void external_call_depth_inc(void) {
    ++external_call_depth;
}

void external_call_depth_dec(void) {
    --external_call_depth;
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    if (external_call_depth == 0) {
        gc_collect_top_level();
    }
    #endif
}

/* ---- Stack size ---- */
#define CSTACK_SIZE (32 * 1024)

/* ---- GC: top-level collection for split-heap-auto mode ---- */
#if MICROPY_GC_SPLIT_HEAP_AUTO
static bool gc_collect_pending = false;

size_t gc_get_max_new_split(void) {
    return 128 * 1024 * 1024;
}

/* Don't collect inline; flag for collection at the top level */
void gc_collect(void) {
    gc_collect_pending = true;
}

static void gc_collect_top_level(void) {
    if (gc_collect_pending) {
        gc_collect_pending = false;
        gc_collect_start();
        gc_collect_end();
    }
}

#else

static void gc_scan_func(void *begin, void *end) {
    gc_collect_root((void **)begin, (void **)end - (void **)begin + 1);
}

void gc_collect(void) {
    gc_collect_start();
    /* emscripten_scan_registers is not available without Asyncify in
     * Emscripten 5.x.  We scan the stack only — sufficient for synchronous
     * WASM where GC fires at safe points with no live mp_obj_t registers. */
    emscripten_scan_stack(gc_scan_func);
    gc_collect_end();
}

#endif  /* MICROPY_GC_SPLIT_HEAP_AUTO */

/* ---- mp_js_init ---- */

void mp_js_init(int pystack_size, int heap_size) {
    mp_cstack_init_with_sp_here(CSTACK_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    /* DTCM semantics: pystack buffer will be checkpointed to /mem/pystack
     * on suspend.  Phase 1: simple malloc.  Phase 2: back with a memfs file.*/
    mp_obj_t *pystack = (mp_obj_t *)malloc(pystack_size * sizeof(mp_obj_t));
    mp_pystack_init(pystack, pystack + pystack_size);
    #endif

    #if MICROPY_ENABLE_GC
    /* DTCM semantics: GC heap will be checkpointed to /mem/heap on suspend.
     * Phase 1: simple malloc.  Phase 2: back with a memfs-mapped region.   */
    char *heap = (char *)malloc(heap_size);
    gc_init(heap, heap + heap_size);
    #endif

    #if MICROPY_GC_SPLIT_HEAP_AUTO
    MP_STATE_MEM(gc_alloc_threshold) = 16 * 1024 / MICROPY_BYTES_PER_GC_BLOCK;
    #endif

    /* Create Emscripten MEMFS virtual hardware bus (/dev/, /mem/, etc.) */
    mp_js_init_filesystem();   /* library.js: creates directory tree + registers capture devices */
    vdev_init();               /* C: opens hot-path FDs, seeds device files */

    /* Redirect sys.stdout / sys.stderr to our capture devices.
     * extmod/vfs_posix_file.c defines mp_sys_stdout_obj with fd=STDOUT_FILENO (1),
     * which routes to the real terminal.  We patch the fd after vdev_init()
     * so Python print() goes through /dev/py_stdout → Module._pyStdout. */
    if (vdev_stdout_fd >= 0) { mp_sys_stdout_obj.fd = vdev_stdout_fd; }
    if (vdev_stderr_fd >= 0) { mp_sys_stderr_obj.fd = vdev_stderr_fd; }

    mp_init();

    /* Mount /flash as the VFS root so Python can import from /flash/lib */
    #if MICROPY_VFS_POSIX
    {
        mp_obj_t path_args[1] = {
            MP_OBJ_NEW_QSTR(qstr_from_str("/flash")),
        };
        mp_obj_t vfs = MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(
            &mp_type_vfs_posix, 1, 0, path_args);
        mp_obj_t mount_args[2] = {
            vfs,
            MP_OBJ_NEW_QSTR(qstr_from_str("/")),
        };
        mp_vfs_mount(2, mount_args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    /* Remove '' from sys.path (added by mp_init via runtime.c).
     * '' means "current directory" but POSIX VFS has a mismatch: import_stat
     * prepends the root to relative paths but open() does not, causing OSError
     * when opening a module that was found by stat.  Use absolute paths only. */
    mp_obj_list_remove(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("")));
    /* Add '/' (= /flash/ in MEMFS) so modules in /flash/ can be imported. */
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/")));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    /* Add '/circuitpy' so user code in /flash/circuitpy/ can be imported.
     * In the browser this directory is backed by IndexedDB (IDBFS). */
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/circuitpy")));
    #endif

    /* Initialise proxy system for Python↔JS FFI */
    proxy_c_init();
}

/* ---- mp_js_register_js_module ---- */

void mp_js_register_js_module(const char *name, uint32_t *value) {
    mp_obj_t module_name = MP_OBJ_NEW_QSTR(qstr_from_str(name));
    mp_obj_t module = proxy_convert_js_to_mp_obj_cside(value);
    mp_map_t *loaded = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_lookup(loaded, module_name, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = module;
}

/* ---- mp_js_do_import ---- */

void mp_js_do_import(const char *name, uint32_t *out) {
    external_call_depth_inc();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t ret = mp_import_name(qstr_from_str(name), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        /* Return leaf of "a.b.c" → "c" */
        const char *m = name, *n = name;
        for (;; ++n) {
            if (*n == '\0' || *n == '.') {
                if (m != name) {
                    ret = mp_load_attr(ret, qstr_from_strn(m, n - m));
                }
                m = n + 1;
                if (*n == '\0') {
                    break;
                }
            }
        }
        nlr_pop();
        proxy_convert_mp_to_js_obj_cside(ret, out);
    } else {
        proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
    }
    external_call_depth_dec();
}

/* ---- mp_js_do_exec_abortable — internal exec with abort support ---- */

/*
 * Execute src.  Returns false on normal exit or Python exception, true if
 * the VM was aborted by a timeout (signalled via mp_sched_keyboard_interrupt).
 *
 * Timeout abort: mp_js_hook() calls mp_sched_keyboard_interrupt() when the
 * deadline fires.  This raises KeyboardInterrupt via the normal NLR path.
 * We detect abort by checking whether the caught exception is KeyboardInterrupt
 * AND mp_js_run already cleared the deadline (meaning it was a timeout, not
 * user-initiated).  A simpler heuristic: if _run_deadline was cleared by the
 * hook (set to 0), this was a timeout abort.
 */
static bool mp_js_do_exec_abortable(const char *src, size_t len, uint32_t *out) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(
            MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_obj_t ret = mp_call_function_0(module_fun);
        nlr_pop();
        proxy_convert_mp_to_js_obj_cside(ret, out);
        return false;
    } else {
        mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
        /* Detect host-requested abort (timeout or /dev/interrupt).
         * mp_js_host_aborted() is set by mp_hal_hook() in both cases, and
         * cleared at the start of each run by mp_js_set_deadline().
         * Python code that does `raise KeyboardInterrupt()` does NOT set it. */
        bool aborted = (mp_js_host_aborted() &&
                        mp_obj_is_exception_instance(exc) &&
                        mp_obj_exception_match(exc, &mp_type_KeyboardInterrupt));
        if (!aborted) {
            mp_obj_print_exception(&mp_sys_stderr_print, exc);
            proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
        } else {
            proxy_convert_mp_to_js_obj_cside(mp_const_none, out);
        }
        return aborted;
    }
}

/* ---- mp_js_do_exec ---- */

void mp_js_do_exec(const char *src, size_t len, uint32_t *out) {
    external_call_depth_inc();
    mp_js_do_exec_abortable(src, len, out);
    external_call_depth_dec();
}

/* ---- mp_js_run — new: timeout + state delta ---- */

/*
 * Execute src with a timeout.  Returns 0 on normal exit, 1 if aborted.
 *
 * Before execution:  snapshot globals → /state/snapshot.json
 * JS sets deadline:  Module._run_deadline = Date.now() + timeout_ms
 *                    mp_js_hook() fires mp_sched_vm_abort() when elapsed.
 * After execution:   diff globals, capture stdout → /state/result.json
 *
 * The caller (JS worker) reads /state/result.json for the result payload.
 */
int mp_js_run(const char *src, size_t len, uint32_t timeout_ms, uint32_t *out) {
    mp_memfs_snapshot_globals();
    uint32_t t0 = (uint32_t)mp_js_ticks_ms();
    mp_js_set_deadline(t0 + timeout_ms);
    external_call_depth_inc();
    bool aborted = mp_js_do_exec_abortable(src, len, out);
    external_call_depth_dec();
    mp_js_set_deadline(0);
    mp_js_hook();  /* flush any bc_out written during last bytecodes (e.g. _thread.start_new_thread) */
    mp_memfs_finish_run(aborted, t0);
    return aborted ? 1 : 0;
}

/* ---- mp_js_compile_to_mpy — compile Python source to .mpy bytecode ---- */

/*
 * Compile src (Python source) and write .mpy bytecode to /flash/<name>.mpy
 * via VFS (which maps to /flash/ in MEMFS).
 *
 * On success: returns 0, .mpy is at MEMFS path /flash/<name>.mpy
 * On error:   prints exception to stderr, returns -1
 */
int mp_js_compile_to_mpy(const char *name, const char *src, size_t src_len) {
    /* Build the MEMFS absolute path: /flash/<name>.mpy
     * mp_raw_code_save_file() uses POSIX open() directly (bypasses MicroPython VFS),
     * so we must use the raw MEMFS path, not the VFS-relative path. */
    char mpy_path[256];
    snprintf(mpy_path, sizeof(mpy_path), "/flash/%s.mpy", name);

    external_call_depth_inc();
    nlr_buf_t nlr;
    int rc = -1;
    if (nlr_push(&nlr) == 0) {
        /* Parse */
        mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(
            qstr_from_str(name), src, src_len, 0);
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);

        /* Compile to raw code */
        mp_compiled_module_t cm;
        mp_compile_to_raw_code(&parse_tree, qstr_from_str(name), false, &cm);

        /* Serialize to .mpy via VFS */
        mp_raw_code_save_file(&cm, qstr_from_str(mpy_path));

        nlr_pop();
        rc = 0;
    } else {
        mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
        mp_obj_print_exception(&mp_sys_stderr_print, exc);
    }
    external_call_depth_dec();
    return rc;
}

/* ---- mp_js_checkpoint_vm / mp_js_restore_vm ---- */

void mp_js_checkpoint_vm(void) {
    mp_memfs_checkpoint_vm();
}

void mp_js_restore_vm(void) {
    mp_memfs_restore_vm();
}

/* ---- mp_js_enable_trace — enable/disable sys.settrace to /debug/trace.json ---- */

#if MICROPY_PY_SYS_SETTRACE

/* Forward declaration so mp_trace_hook can return itself. */
MP_DECLARE_CONST_FUN_OBJ_3(mp_trace_hook_obj);

/*
 * C-native trace hook: (frame, event, arg) → self
 *
 * Called by MicroPython's profile machinery for call/line/return/exception events.
 * Emits a NDJSON record to /debug/trace.json and returns itself as the per-frame
 * callback so all event types are captured.
 */
static mp_obj_t mp_trace_hook(mp_obj_t frame_obj, mp_obj_t event_obj, mp_obj_t arg) {
    (void)arg;
    mp_obj_frame_t *frame = MP_OBJ_TO_PTR(frame_obj);
    mp_obj_code_t  *code  = frame->code;
    const mp_raw_code_t *rc = code->rc;
    const mp_bytecode_prelude_t *prelude = &rc->prelude;

    /* Source file is at qstr_table[0]; function name at qstr_table[qstr_block_name_idx] */
    const char *file_str = qstr_str(MP_CODE_QSTR_MAP(code->context, 0));
    const char *name_str = qstr_str(MP_CODE_QSTR_MAP(code->context, prelude->qstr_block_name_idx));

    /* Event is a Python string (call / line / return / exception) */
    size_t event_len;
    const char *event_str = mp_obj_str_get_data(event_obj, &event_len);

    /* Build the JSON record — file/name are qstr-interned identifiers (no quotes inside) */
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"event\":\"%.*s\",\"lineno\":%u,\"file\":\"%s\",\"name\":\"%s\"}",
        (int)event_len, event_str,
        (unsigned)frame->lineno,
        file_str,
        name_str);

    mp_memfs_trace_append(buf);

    /* Return self → becomes per-frame callback for line/return/exception events */
    return MP_OBJ_FROM_PTR(&mp_trace_hook_obj);
}
MP_DEFINE_CONST_FUN_OBJ_3(mp_trace_hook_obj, mp_trace_hook);

#endif  /* MICROPY_PY_SYS_SETTRACE */

/*
 * Enable or disable the built-in trace hook.
 * on=1: truncate /debug/trace.json and start tracing
 * on=0: stop tracing
 */
void mp_js_enable_trace(int on) {
    #if MICROPY_PY_SYS_SETTRACE
    if (on) {
        int fd = open("/debug/trace.json", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) { close(fd); }
        mp_prof_settrace(MP_OBJ_FROM_PTR(&mp_trace_hook_obj));
    } else {
        mp_prof_settrace(mp_const_none);
    }
    #endif
}

/* ---- VFS stubs (used when MICROPY_VFS is not enabled) ---- */

#if !MICROPY_VFS
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_raise_OSError(MP_ENOENT);
}
mp_import_stat_t mp_import_stat(const char *path) {
    return MP_IMPORT_STAT_NO_EXIST;
}
mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
#endif

/* ---- Fault handlers ---- */

void nlr_jump_fail(void *val) {
    while (1) {
        ;
    }
}

/* ===========================================================================
 * Step-wise VM execution (libpyvm)
 *
 * mp_vm_start(src, len)  — compile code, create initial code_state on pystack
 * mp_vm_step()           — execute one batch of bytecodes; returns status:
 *                           0 = normal completion
 *                           1 = yielded (suspended, call step() again to resume)
 *                           2 = exception
 * mp_vm_result_stdout()  — after completion, read captured stdout
 * mp_vm_result_stderr()  — after completion, read captured stderr
 *
 * The step budget is set from JS: Module._vm_step_budget = N
 * before each mp_vm_step() call.  The VM executes up to N branch points
 * (loop iterations, if/else, function calls), then suspends.
 *
 * Between steps, JS can:
 *   - Yield to the browser event loop (await setTimeout(0))
 *   - Drain bc_out and broadcast hardware events
 *   - Sync hardware registers from bc_in
 *   - Process async callbacks and background tasks
 * =========================================================================== */

#if MICROPY_VM_YIELD_ENABLED

/* Persistent state between mp_vm_start() and mp_vm_step() calls */
static mp_code_state_t *_vm_step_code_state = NULL;       /* outermost (module) frame */
static mp_obj_t         _vm_step_module_fun = MP_OBJ_NULL;
static bool             _vm_step_started = false;
static bool             _vm_step_first_entry = false;

/* Written by the VM yield handler (MICROPY_VM_YIELD_SAVE_STATE in vm.c).
 * Contains the innermost code_state at the point of yield so we can
 * resume there instead of at the outermost frame. */
void *mp_vm_yield_state = NULL;

/*
 * Compile src and prepare code_state for stepping.
 * Returns 0 on success, -1 on compile error (exception printed to stderr).
 */
int mp_vm_start(const char *src, size_t len) {
    /* Reset any previous stepping state */
    _vm_step_code_state = NULL;
    _vm_step_module_fun = MP_OBJ_NULL;
    _vm_step_started = false;

    /* Compile the source */
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(
            MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        _vm_step_module_fun = mp_compile(&parse_tree, source_name, false);
        nlr_pop();
    } else {
        /* Compile error */
        mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
        mp_obj_print_exception(&mp_sys_stderr_print, exc);
        return -1;
    }

    /* Prepare code_state on the pystack.
     * mp_obj_fun_bc_prepare_codestate allocates on pystack (LIFO),
     * sets up locals/stack, and saves old_globals. */
    _vm_step_code_state = mp_obj_fun_bc_prepare_codestate(
        _vm_step_module_fun, 0, 0, NULL);
    if (_vm_step_code_state == NULL) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("cannot create code state for stepping"));
        return -1;
    }
    _vm_step_code_state->prev = NULL;  /* top-level: no parent frame */
    _vm_step_started = true;
    _vm_step_first_entry = true;

    /* Snapshot globals for delta tracking */
    mp_memfs_snapshot_globals();
    return 0;
}

/*
 * Execute one batch of bytecodes.  The batch size is controlled by
 * Module._vm_step_budget (set from JS before calling this).
 *
 * Returns:
 *   0 — normal completion (code finished executing)
 *   1 — yielded/suspended (call mp_vm_step() again to resume)
 *   2 — exception (printed to stderr)
 */
int mp_vm_step(void) {
    if (!_vm_step_started || _vm_step_code_state == NULL) {
        return 0;  /* nothing to do */
    }

    mp_vm_return_kind_t ret;
    mp_code_state_t *entry_state;

    if (_vm_step_first_entry) {
        _vm_step_first_entry = false;
        mp_vm_yield_state = NULL;
        entry_state = _vm_step_code_state;
    } else if (mp_vm_yield_state != NULL) {
        /* Resume at the innermost frame that was active when the VM yielded.
         * This is critical for stackless mode: the yield may have fired deep
         * inside a call chain (e.g. inside asyncio.run_until_complete).
         * Without this, we'd re-enter at the outermost <module> frame and
         * re-execute the function call from scratch. */
        entry_state = (mp_code_state_t *)mp_vm_yield_state;
        mp_vm_yield_state = NULL;
    } else {
        entry_state = _vm_step_code_state;
    }

    ret = mp_execute_bytecode(entry_state, MP_OBJ_NULL);

    switch (ret) {
        case MP_VM_RETURN_NORMAL:
            /* Code finished.  Restore globals, write result. */
            mp_globals_set(_vm_step_code_state->old_globals);
            _vm_step_started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_vm_step_code_state);
            #endif
            _vm_step_code_state = NULL;
            return 0;

        case MP_VM_RETURN_YIELD:
            /* Suspended — code_state is preserved on pystack.
             * JS does background work, then calls mp_vm_step() again. */
            return 1;

        case MP_VM_RETURN_EXCEPTION:
            /* Exception.  Print it and clean up. */
            mp_globals_set(_vm_step_code_state->old_globals);
            {
                mp_obj_t exc = MP_OBJ_FROM_PTR(_vm_step_code_state->state[0]);
                mp_obj_print_exception(&mp_sys_stderr_print, exc);
            }
            _vm_step_started = false;
            #if MICROPY_ENABLE_PYSTACK
            mp_pystack_free(_vm_step_code_state);
            #endif
            _vm_step_code_state = NULL;
            return 2;

        default:
            return 2;
    }
}

#endif /* MICROPY_VM_YIELD_ENABLED */


/* ===========================================================================
 * GC heap and pystack accessors (Phase 9: libpygc / libpystack)
 *
 * These return WASM linear memory offsets and sizes so JS (api.js) can
 * create typed array views (Module.HEAPU8.subarray) for direct access,
 * snapshot, and restore without MEMFS round-trips.
 * =========================================================================== */

/* GC heap: ATB + block pool, saved as one contiguous region */
int mp_gc_heap_addr(void) {
    return (int)(uintptr_t)MP_STATE_MEM(area.gc_alloc_table_start);
}
int mp_gc_heap_size(void) {
    return (int)((uintptr_t)MP_STATE_MEM(area.gc_pool_end) -
                 (uintptr_t)MP_STATE_MEM(area.gc_alloc_table_start));
}
int mp_gc_pool_used(void) {
    gc_info_t info;
    gc_info(&info);
    return (int)info.used;
}
int mp_gc_pool_total(void) {
    gc_info_t info;
    gc_info(&info);
    return (int)info.total;
}
int mp_gc_pool_free(void) {
    gc_info_t info;
    gc_info(&info);
    return (int)info.free;
}

/* mp_state_ctx: global VM state struct (dict_main, qstr tables, etc.) */
int mp_state_ctx_addr(void) {
    return (int)(uintptr_t)&mp_state_ctx;
}
int mp_state_ctx_size(void) {
    return (int)sizeof(mp_state_ctx);
}

/* Pystack: evaluation temporaries, code_state chain (stackless mode) */
int mp_pystack_get_addr(void) {
    return (int)(uintptr_t)MP_STATE_THREAD(pystack_start);
}
int mp_pystack_get_size(void) {
    return (int)((uintptr_t)MP_STATE_THREAD(pystack_end) -
                 (uintptr_t)MP_STATE_THREAD(pystack_start));
}
int mp_pystack_get_used(void) {
    return (int)((uintptr_t)MP_STATE_THREAD(pystack_cur) -
                 (uintptr_t)MP_STATE_THREAD(pystack_start));
}


void __fatal_error(const char *msg) NORETURN;
void __fatal_error(const char *msg) {
    (void)msg;
    while (1) {
        ;
    }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    __fatal_error("Assertion failed");
}
#endif
