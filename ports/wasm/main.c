/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023-2024 Damien P. George
 * Copyright (c) 2024 Joey Castillo
 * CIRCUITPY-CHANGE: Copyright (c) 2025 CircuitPython contributors
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
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "shared/runtime/pyexec.h"
#include "shared/runtime/gchelper.h"

#include "emscripten.h"
#include "lexer_dedent.h"
#include "library.h"
#include "proxy_c.h"
#include "supervisor/port.h"
#include "supervisor/background_callback.h"

// =============================================================================
// EXTERNAL CALL DEPTH TRACKING
// =============================================================================

// This counter tracks the current depth of calls into C code that originated
// externally, i.e., from JavaScript.
static size_t external_call_depth = 0;

void external_call_depth_inc(void) {
    ++external_call_depth;
    #if MICROPY_GC_SPLIT_HEAP_AUTO
    if (external_call_depth == 1) {
        gc_collect_top_level();  // GC at top-level!
    }
    #endif
}

void external_call_depth_dec(void) {
    --external_call_depth;
}

size_t external_call_depth_get(void) {
    return external_call_depth;
}

// =============================================================================
// GARBAGE COLLECTION
// =============================================================================

#if MICROPY_GC_SPLIT_HEAP_AUTO

static bool gc_collect_pending = false;

// The largest new region that is available to become Python heap.
size_t gc_get_max_new_split(void) {
    return 128 * 1024 * 1024;
}

// Don't collect anything. Instead require the heap to grow.
void gc_collect(void) {
    gc_collect_pending = true;
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

void gc_collect(void) {
    gc_collect_start();
    gc_collect_end();
}

// =============================================================================
// OUTPUT FUNCTIONS
// =============================================================================

EM_JS(void, mp_js_write_js, (const char *buf, size_t len), {
    if (len > 0) {
        const text = UTF8ToString(buf, len);
        if (Module.onStdout) {
            Module.onStdout(text);
        } else {
            console.log(text);
        }
    }
});

void mp_js_write(const char *buf, mp_uint_t len) {
    mp_js_write_js(buf, len);
}

const mp_print_t mp_js_stdout_print = {NULL, (mp_print_strn_t)mp_js_write};

// =============================================================================
// MICROPYTHON INITIALIZATION
// =============================================================================

void mp_js_init(int pystack_size, int heap_size) {
    // Initialize C stack limit checking
    // Use 40KB as the stack size limit (Emscripten default is generous)
    mp_cstack_init_with_sp_here(40 * 1024);

    #if MICROPY_ENABLE_PYSTACK
    // Allocate pystack dynamically based on requested size
    mp_obj_t *pystack = (mp_obj_t *)malloc(pystack_size * sizeof(mp_obj_t));
    mp_pystack_init(pystack, &pystack[pystack_size]);
    #endif

    #if MICROPY_ENABLE_GC
    char *heap = (char *)malloc(heap_size * sizeof(char));
    gc_init(heap, heap + heap_size);
    #endif

    #if MICROPY_GC_SPLIT_HEAP_AUTO
    // Set a smaller initial allocation threshold for GC.  This helps
    // to trigger the initial GC collection before the heap gets too big.
    // The actual garbage collection will happen later when control returns to the top-level,
    // via the `gc_collect_pending` flag and `gc_collect_top_level()`.
    MP_STATE_MEM(gc_alloc_threshold) = 16 * 1024 / MICROPY_BYTES_PER_GC_BLOCK;
    #endif

    mp_init();

    #if MICROPY_VFS_POSIX
    {
        // Mount the host FS at the root of our internal VFS
        mp_obj_t args[2] = {
            MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(qstr_from_str("/")),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR__slash_lib));
    #endif
}

// =============================================================================
// JAVASCRIPT MODULE REGISTRATION
// =============================================================================

void mp_js_register_js_module(const char *name, uint32_t *value) {
    mp_obj_t module_name = MP_OBJ_NEW_QSTR(qstr_from_str(name));
    mp_obj_t module = proxy_convert_js_to_mp_obj_cside(value);
    mp_map_t *mp_loaded_modules_map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_lookup(mp_loaded_modules_map, module_name, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND)->value = module;
}

// =============================================================================
// IMPORT FUNCTION
// =============================================================================

void mp_js_do_import(const char *name, uint32_t *out) {
    external_call_depth_inc();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t ret = mp_import_name(qstr_from_str(name), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        // Return the leaf of the import, e.g., for "a.b.c" return "c".
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

// =============================================================================
// PYTHON EXECUTION
// =============================================================================

void mp_js_do_exec(const char *src, size_t len, uint32_t *out) {
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
}

void mp_js_do_exec_async(const char *src, size_t len, uint32_t *out) {
    mp_compile_allow_top_level_await = true;
    mp_js_do_exec(src, len, out);
    mp_compile_allow_top_level_await = false;
}

// =============================================================================
// REPL FUNCTIONS
// =============================================================================

void mp_js_repl_init(void) {
    pyexec_event_repl_init();
}

int mp_js_repl_process_char(int c) {
    external_call_depth_inc();
    int ret = pyexec_event_repl_process_char(c);
    external_call_depth_dec();
    return ret;
}

// =============================================================================
// MAIN LOOP - COOPERATIVE YIELDING IMPLEMENTATION
// =============================================================================

// Forward declarations
extern volatile bool wasm_should_yield_to_js;
extern void wasm_reset_yield_state(void);

// Main loop state
typedef struct {
    bool repl_active;
    bool python_running;
} main_loop_state_t;

static main_loop_state_t main_state = {
    .repl_active = false,
    .python_running = false,
};

// One iteration of the main loop
// This function is called repeatedly by emscripten_set_main_loop
void main_loop_iteration(void* arg) {
    (void)arg;  // Unused
    
    // Reset yield state at the start of each iteration
    wasm_reset_yield_state();
    
    // Run background callbacks
    // These execute at safe points in the VM
    if (background_callback_pending()) {
        background_callback_run_all();
    }
    
    // If REPL is active, continue processing
    // The REPL will yield naturally via RUN_BACKGROUND_TASKS
    if (main_state.repl_active) {
        // REPL continues from its internal state
        // Will yield when wasm_should_yield_to_js becomes true
    }
    
    // Function returns here, yielding to JavaScript
    // JavaScript's requestAnimationFrame will call this function again
}

// =============================================================================
// ERROR HANDLING
// =============================================================================

void nlr_jump_fail(void *val) {
    // DEBUG: Print call depth to understand context
    fprintf(stderr, "FATAL: uncaught exception %p (external_call_depth=%zu)\n",
            val, external_call_depth);

    // Try to print exception details (this might itself cause issues)
    // mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(val));

    // For now, just exit without trying to print the exception
    emscripten_force_exit(1);
}

// CIRCUITPY-CHANGE: Required by CircuitPython
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    printf("Assertion '%s' failed, at file %s:%d\n", expr, file, line);
    emscripten_force_exit(1);
}

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================

int main(int argc, char **argv) {
    // Parse command-line arguments
    int pystack_size = 2 * 1024;  // Default 2KB (in words)
    int heap_size = 128 * 1024;   // Default 128KB

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-X") == 0 && i + 1 < argc) {
            if (strncmp(argv[i + 1], "heapsize=", 9) == 0) {
                heap_size = atoi(argv[i + 1] + 9);
                i++;
            }
        }
    }

    // Initialize MicroPython
    mp_js_init(pystack_size, heap_size);

    // Initialize port (sets up peripherals, timing, etc.)
    safe_mode_t safe_mode = port_init();
    (void)safe_mode;  // TODO: Handle safe mode

    // NOTE: REPL initialization is handled by JavaScript via replInit()
    // This avoids double banner printing and gives web apps control over when REPL starts
    // main_state.repl_active will be set when replInit() is called

    // Give control to the browser event loop
    // Parameters:
    //   - main_loop_iteration: Function to call each frame
    //   - &main_state: Argument passed to the function
    //   - 0: Use requestAnimationFrame (~60fps)
    //   - 1: Simulate infinite loop (keeps calling the function)
    emscripten_set_main_loop_arg(main_loop_iteration, &main_state, 0, 1);
    
    // This point is never reached
    return 0;
}