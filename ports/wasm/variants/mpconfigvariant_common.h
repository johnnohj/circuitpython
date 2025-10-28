/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Damien P. George
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

// CIRCUITPY-CHANGE: Common variant configuration for WASM builds

// This file contains common configuration across WASM variants
// Based on Unix port's variant settings but adapted for WebAssembly

#define MICROPY_COMP_CONST_FOLDING  (1)
#define MICROPY_COMP_CONST_LITERAL  (1)
#define MICROPY_COMP_DOUBLE_TUPLE_ASSIGN (1)

#define MICROPY_OPT_CACHE_MAP_LOOKUP_IN_BYTECODE (1)
#define MICROPY_OPT_MPZ_BITWISE     (1)

#define MICROPY_READER_POSIX        (1)
#define MICROPY_READER_VFS          (1)

#define MICROPY_PY_BUILTINS_HELP    (1)
#define MICROPY_PY_BUILTINS_HELP_TEXT circuitpython_help_text
#define MICROPY_PY_BUILTINS_HELP_MODULES (1)

#define MICROPY_PY_IO_BUFFEREDWRITER (1)
#define MICROPY_PY_FSTRINGS         (1)
#define MICROPY_PY_SYS_STDFILES     (1)
#define MICROPY_PY_SYS_STDIO_BUFFER (1)

// CIRCUITPY-CHANGE: Enable additional debugging/introspection features
#define MICROPY_DEBUG_PRINTERS      (1)
#define MICROPY_ENABLE_EMERGENCY_EXCEPTION_BUF (1)
#define MICROPY_EMERGENCY_EXCEPTION_BUF_SIZE (256)

// Configure the "sys" module with features not usually enabled on bare-metal.
#define MICROPY_PY_SYS_ATEXIT          (1)
#define MICROPY_PY_SYS_EXC_INFO        (1)

// Memory/GC settings
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)
#define MICROPY_REPL_INFO           (1)

// Return number of collected objects from gc.collect().
#define MICROPY_PY_GC_COLLECT_RETVAL   (1)

// Allow loading of .mpy files.
#define MICROPY_PERSISTENT_CODE_LOAD   (1)

// Float settings
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_CPYTHON_COMPAT      (1)

// Allow keyboard interrupt from signal handler
#define MICROPY_ASYNC_KBD_INTR      (1)

// REPL conveniences.
#define MICROPY_REPL_EMACS_WORDS_MOVE  (1)
#define MICROPY_REPL_EMACS_EXTRA_WORDS_MOVE (1)
#define MICROPY_USE_READLINE_HISTORY   (1)
#ifndef MICROPY_READLINE_HISTORY_SIZE
#define MICROPY_READLINE_HISTORY_SIZE  (100)
#endif
