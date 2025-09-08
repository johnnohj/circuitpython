// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython WebAssembly Contributors
//
// SPDX-License-Identifier: MIT

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
#include "shared/runtime/pyexec.h"

#include "emscripten.h"

#if CIRCUITPY_HAL_PROVIDER
#include "hal_provider.h"
// Provider declarations
extern const hal_provider_t hal_stub_provider;
extern const hal_provider_t hal_js_provider;
#endif

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

void mp_js_init(int pystack_size, int heap_size) {
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

    #if CIRCUITPY_HAL_PROVIDER
    // Initialize HAL provider system
    hal_provider_init();
    
    // Register default stub provider
    hal_register_provider(&hal_stub_provider);
    #endif

    // Note: sys module initialization handled by MicroPython core
}

void mp_js_init_with_heap(int heap_size) {
    mp_js_init(8 * 1024, heap_size);
}

void mp_js_repl_init(void) {
    // Initialize REPL - basic initialization for now
    // The actual REPL setup can be done in JavaScript
}

int mp_js_repl_process_char(int c) {
    external_call_depth_inc();
    // Basic character processing - placeholder for now
    int ret = 0;
    external_call_depth_dec();
    return ret;
}

#if MICROPY_GC_SPLIT_HEAP_AUTO

static bool gc_collect_pending = false;

// The largest new region that is available to become Python heap.
size_t gc_get_max_new_split(void) {
    return 128 * 1024 * 1024;
}

// Don't collect anything.  Instead require the heap to grow.
void gc_collect(void) {
    gc_collect_pending = true;
}

// Collect at the top-level, where there are no root pointers from stack/registers.
static void gc_collect_top_level(void) {
    if (gc_collect_pending) {
        gc_collect_pending = false;
        gc_collect_root((void**)&MP_STATE_THREAD(pystack_start), (((char*)MP_STATE_THREAD(pystack_cur) - (char*)MP_STATE_THREAD(pystack_start)) / sizeof(mp_obj_t)));
    }
}

#endif

int mp_hal_get_interrupt_char(void) {
    return -1; // Indicate no interrupt character
}

void mp_hal_stdout_tx_strn(const char *str, size_t len) {
    // Output to JavaScript console
    EM_ASM({
        var s = UTF8ToString($0, $1);
        console.log(s);
    }, str, len);
}

// Main function for Emscripten
int main() {
    // WebAssembly doesn't use main in the traditional sense
    // Initialization happens through JavaScript calls
    return 0;
}