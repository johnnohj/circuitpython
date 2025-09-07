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

// Set base feature level.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)

// CIRCUITPY-CHANGE: Enable JavaScript FFI for WebAssembly build
#define MICROPY_PY_JSFFI (1)

// CIRCUITPY-CHANGE: Enable event-driven REPL for WebAssembly
#define MICROPY_REPL_EVENT_DRIVEN (1)

// CIRCUITPY-CHANGE: Use system printf for WebAssembly (avoid symbol conflicts)
#define MICROPY_USE_INTERNAL_PRINTF (0)

// CIRCUITPY-CHANGE: Disable modules not compatible with WebAssembly
#define MICROPY_PY_MICROPYTHON_RINGIO (0)

// Enable extra Unix features.
#include "../mpconfigvariant_common.h"

// Override Unix-specific settings for WebAssembly
#undef MICROPY_PY_OS
#define MICROPY_PY_OS                    (0)

#undef MICROPY_PY_OS_INCLUDEFILE
#undef MICROPY_PY_OS_ERRNO
#undef MICROPY_PY_OS_GETENV_PUTENV_UNSETENV
#undef MICROPY_PY_OS_SYSTEM
#undef MICROPY_PY_OS_URANDOM

#undef MICROPY_PY_TERMIOS
#define MICROPY_PY_TERMIOS               (0)

// Disable threading completely for WebAssembly
#undef MICROPY_PY_THREAD
#define MICROPY_PY_THREAD                (0)

// Disable FFI for WebAssembly
#undef MICROPY_PY_FFI
#define MICROPY_PY_FFI                   (0)

// Disable VFS POSIX for WebAssembly (symbol conflicts)
#undef MICROPY_VFS_POSIX
#define MICROPY_VFS_POSIX                (0)

// Disable ulab for now (scientific computing library)
#undef CIRCUITPY_ULAB
#define CIRCUITPY_ULAB                   (0)

// Enable JavaScript-backed I/O modules
#define CIRCUITPY_DIGITALIO              (1)
#define CIRCUITPY_ANALOGIO               (1)
#define CIRCUITPY_BUSIO                  (1)
