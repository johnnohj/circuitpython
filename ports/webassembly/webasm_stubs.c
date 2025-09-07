/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Damien P. George
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

// Stubs for WebAssembly build

#include "py/obj.h"

// Stub for input.c prompt function (we excluded input.c)
char *prompt(const char *p) {
    (void)p;
    return NULL;
}

// Stub for ulab module (we disabled it but it's still referenced)
const mp_obj_module_t ulab_user_cmodule = {
    .base = { &mp_type_module },
    .globals = NULL,
};

// Stub for gc_helper_collect_regs_and_stack 
// WebAssembly doesn't need stack scanning in the same way
#include "py/gc.h"
void gc_helper_collect_regs_and_stack(void) {
    // WebAssembly stub - no register/stack scanning needed
    // The WASM runtime manages this for us
}