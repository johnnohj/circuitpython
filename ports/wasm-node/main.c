/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2014-2017 Paul Sokolovsky
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/objstr.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/nlr.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "shared/runtime/pyexec.h"

// CIRCUITPY-CHANGE: Add CircuitPython supervisor includes
#include "supervisor/board.h"
#include "supervisor/port.h"
#include "supervisor/shared/translate/translate.h"
#include "supervisor/shared/tick.h"

#include "emscripten.h"
#include "lexer_dedent.h"
#include "library.h"
#include "hal_provider.h"
#ifndef CIRCUITPY_CORE
#include "proxy_c.h"
#endif

// External provider declarations
extern const hal_provider_t nodejs_hal_provider;

// This counter tracks the current depth of calls into C code that originated
// externally, ie from JavaScript.  When the counter is 0 that corresponds to
// the top-level call into C.
static size_t external_call_depth = 0;

// Emscripten defaults to a 64k C-stack, so our limit should be less than that.
#define CSTACK_SIZE (32 * 1024)

#if MICROPY_GC_SPLIT_HEAP_AUTO
static void gc_collect_top_level(void);
#endif

void external_call_depth_inc(void) {
    ++external_call_depth;
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    if (external_call_depth == 1) {
        gc_collect_top_level();
    }
    #endif
}

void external_call_depth_dec(void) {
    --external_call_depth;
}

// CIRCUITPY-CHANGE: Provide stderr print implementation
static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    printf("%.*s", (int)len, str);
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};


void mp_js_init(int pystack_size, int heap_size) {
    // CIRCUITPY-CHANGE: Skip early board_init() to prevent proxy crashes
    // Board initialization will be deferred until mp_js_post_init()

    mp_cstack_init_with_sp_here(CSTACK_SIZE);

    #if MICROPY_ENABLE_PYSTACK
    mp_obj_t *pystack = (mp_obj_t *)malloc(pystack_size * sizeof(mp_obj_t));
    mp_pystack_init(pystack, pystack + pystack_size);
    #endif

    #if MICROPY_ENABLE_GC
    char *heap = (char *)malloc(heap_size * sizeof(char));
    gc_init(heap, heap + heap_size);
    #endif

    #if MICROPY_GC_SPLIT_HEAP_AUTO
    // When MICROPY_GC_SPLIT_HEAP_AUTO is enabled, set the GC threshold to a low
    // value so that a collection is triggered before the heap fills up.  The actual
    // garbage collection will happen later when control returns to the top-level,
    // via the `gc_collect_pending` flag and `gc_collect_top_level()`.
    MP_STATE_MEM(gc_alloc_threshold) = 16 * 1024 / MICROPY_BYTES_PER_GC_BLOCK;
    #endif

    mp_init();

    // CIRCUITPY: Initialize HAL provider system for Node.js
    hal_provider_init();

    // Register Node.js-specific hardware provider
    hal_register_provider(&nodejs_hal_provider);

    // Initialize board pins from HAL provider
    extern void hal_board_init_pins(void);
    hal_board_init_pins();

    // WEBASM-FIX: Initialize sys.path early (like Unix port) before any module operations
    // This is critical for proper module imports and prevents "memory access out of bounds" errors
    {
        // sys.path starts as [""] (following Unix port pattern)
        mp_sys_path = mp_obj_new_list(0, NULL);
        mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
        
        // Add WebAssembly-appropriate default paths
        const char *default_path = MICROPY_PY_SYS_PATH_DEFAULT;
        if (default_path) {
            // Parse colon-separated path entries (reusing Unix port logic)
            const char *path = default_path;
            while (*path) {
                const char *path_entry_end = strchr(path, ':');
                if (path_entry_end == NULL) {
                    path_entry_end = path + strlen(path);
                }
                if (path_entry_end > path) {  // Non-empty entry
                    mp_obj_list_append(mp_sys_path, mp_obj_new_str_via_qstr(path, path_entry_end - path));
                }
                path = (*path_entry_end) ? path_entry_end + 1 : path_entry_end;
            }
        }
    }

    // Initialize sys.argv as well (following Unix port pattern)
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);

    // CIRCUITPY-CHANGE: Initialize CircuitPython supervisor services
    // Note: WebAssembly doesn't need continuous background ticking

    #if MICROPY_VFS_POSIX && !DISABLE_FILESYSTEM
    {
        // Mount the host FS at the root of our internal VFS
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(qstr_from_str("/")),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
        
        // Add /lib to sys.path for VFS-mounted library access
        mp_obj_t lib_path = MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib);
        mp_obj_list_append(mp_sys_path, lib_path);
    }
    #endif
}

#ifndef CIRCUITPY_CORE
void mp_js_register_js_module(const char *name, uint32_t *value) {
    mp_obj_t module_name = MP_OBJ_NEW_QSTR(qstr_from_str(name));
    mp_obj_t module = proxy_convert_js_to_mp_obj_cside(value);
    mp_map_t *mp_loaded_modules_map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_lookup(mp_loaded_modules_map, module_name, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = module;
}
#endif

// JavaScript semihosting API functions (from webassembly port)
void mp_js_register_board_config(uint32_t *js_config_ref);
void mp_js_register_board_pins(uint32_t *pins_array, size_t num_pins);
void mp_js_stdin_write_char(int c);
void mp_js_stdin_write_str(const char *str, size_t len);
bool mp_hal_is_stdin_raw_mode(void);

// WEBASM-FIX: Strategic GC collection before memory-intensive module loading
#ifndef CIRCUITPY_CORE
static void gc_collect_before_import(void) {
    #if MICROPY_ENABLE_GC
    if (external_call_depth == 1) {
        // Force garbage collection before module import to prevent fragmentation
        gc_collect();
    }
    #endif
}
#endif

#ifndef CIRCUITPY_CORE
void mp_js_do_import(const char *name, uint32_t *out) {
    external_call_depth_inc();
    
    // WEBASM-FIX: Collect garbage before module loading to prevent memory access errors
    gc_collect_before_import();
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t ret = mp_import_name(qstr_from_str(name), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        // Return the leaf of the import, eg for "a.b.c" return "c".
        const char *m = name;
        const char *n = name;
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
        external_call_depth_dec();
        proxy_convert_mp_to_js_obj_cside(ret, out);
    } else {
        // uncaught exception
        external_call_depth_dec();
        proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
    }
}
#endif

void mp_js_repl_init(void) {
    pyexec_event_repl_init();
}

int mp_js_repl_process_char(int c) {
    external_call_depth_inc();
    int ret = pyexec_event_repl_process_char(c);
    external_call_depth_dec();
    return ret;
}

void mp_js_do_exec(const char *src, size_t len, uint32_t *out) {
#ifndef CIRCUITPY_CORE
    external_call_depth_inc();
    mp_parse_input_kind_t input_kind = MP_PARSE_FILE_INPUT;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_obj_t ret = mp_call_function_0(module_fun);
        nlr_pop();
        external_call_depth_dec();
        proxy_convert_mp_to_js_obj_cside(ret, out);
    } else {
        // uncaught exception
        external_call_depth_dec();
        proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
    }
#else
    // Core interpreter: execute Python code with simple error handling
    external_call_depth_inc();
    mp_parse_input_kind_t input_kind = MP_PARSE_FILE_INPUT;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len_dedent(MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_obj_t ret = mp_call_function_0(module_fun);
        (void)ret; // Suppress unused variable warning
        nlr_pop();
        external_call_depth_dec();
        // For minimal interpreter, we don't convert to JS objects
        // Just indicate success/failure
        if (out) {
            *out = 0; // Success
        }
    } else {
        // uncaught exception - print to stderr and continue
        external_call_depth_dec();
        mp_obj_print_exception(&mp_stderr_print, nlr.ret_val);
        if (out) {
            *out = 1; // Error
        }
    }
#endif
}

void mp_js_do_exec_async(const char *src, size_t len, uint32_t *out) {
#ifndef CIRCUITPY_CORE
    #if MICROPY_PY_ASYNC_AWAIT
    mp_compile_allow_top_level_await = true;
    mp_js_do_exec(src, len, out);
    mp_compile_allow_top_level_await = false;
    #else
    mp_js_do_exec(src, len, out);
    #endif
#else
    // Core interpreter: async support disabled, use regular exec
    mp_js_do_exec(src, len, out);
#endif
}

// Post-initialization function to safely initialize proxy-dependent components
EMSCRIPTEN_KEEPALIVE void mp_js_post_init(void) {
#ifndef CIRCUITPY_CORE
    extern bool proxy_c_is_initialized(void);
    extern void proxy_c_init_safe(void);
    
    // Initialize proxy system safely first
    proxy_c_init_safe();
    
    // Now safe to initialize board after proxy is ready
    if (proxy_c_is_initialized()) {
        board_init();
    }
#else
    // Core interpreter: no JavaScript bridge or board initialization
#endif
}

EMSCRIPTEN_KEEPALIVE void mp_js_init_with_heap(int heap_size) {
    const int pystack_size = 8192;
    mp_js_init(pystack_size, heap_size);
    
    // Automatically perform post-initialization for common use cases
    mp_js_post_init();
}

#if MICROPY_GC_SPLIT_HEAP_AUTO

static bool gc_collect_pending = false;

// The largest new region that is available to become Python heap.
size_t gc_get_max_new_split(void) {
    return 128 * 1024 * 1024;
}

// WEBASM-FIX: Try to add a new heap area when memory is needed (based on MicroPython WebAssembly port)
bool gc_try_add_heap(size_t bytes) {
    // For WebAssembly, we rely on Emscripten's memory growth
    // The heap will grow automatically when more memory is allocated
    // Return false to indicate we don't manually add heap areas
    (void)bytes;
    return false;
}

// Collect at the top-level, where there are no root pointers from stack/registers.
static void gc_collect_top_level(void) {
    if (gc_collect_pending) {
        gc_collect_pending = false;
        gc_collect_start();
        gc_collect_end();
    }
}

#endif

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

void nlr_jump_fail(void *val) {
    while (1) {
        ;
    }
}

void __attribute__((noreturn)) __fatal_error(const char *msg) {
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





#if MICROPY_VFS_ROM_IOCTL

static uint8_t romfs_buf[4] = { 0xd2, 0xcd, 0x31, 0x00 }; // empty ROMFS
static const MP_DEFINE_MEMORYVIEW_OBJ(romfs_obj, 'B', 0, sizeof(romfs_buf), romfs_buf);

mp_obj_t mp_vfs_rom_ioctl(size_t n_args, const mp_obj_t *args) {
    switch (mp_obj_get_int(args[0])) {
        case MP_VFS_ROM_IOCTL_GET_NUMBER_OF_SEGMENTS:
            return MP_OBJ_NEW_SMALL_INT(1);

        case MP_VFS_ROM_IOCTL_GET_SEGMENT:
            return MP_OBJ_FROM_PTR(&romfs_obj);
    }

    return MP_OBJ_NEW_SMALL_INT(-MP_EINVAL);
}

#endif
