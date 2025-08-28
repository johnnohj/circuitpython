/*
 * This file is part of the CircuitPython project, https://circuitpython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 CircuitPython WebAssembly Contributors
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

// CircuitPython WebAssembly Bare Variant Configuration
// Minimal interpreter with core Python functionality only

// Set minimal feature level - just core interpreter
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

// Use system libc to avoid duplicate symbols
#define MICROPY_USE_INTERNAL_PRINTF (0)
#define MICROPY_LIBC_OVERRIDE (0)
#define MICROPY_EMIT_XTENSAWIN (0)

// Disable CircuitPython internal libc to prevent duplicates
#define CIRCUITPY_USE_INTERNAL_LIBC (0)

// Disable features that require hardware or complex system integration
#define MICROPY_PY_MICROPYTHON_MEM_INFO (0)
#define MICROPY_PY_MICROPYTHON_STACK_USE (0)

// Enable only essential VM features
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_FINALISER (1)
#define MICROPY_KBD_EXCEPTION (1)

// Basic I/O and formatting
#define MICROPY_PY_IO (1)
#define MICROPY_PY_SYS_STDIO_BUFFER (1)

// Essential data structures
#define MICROPY_PY_COLLECTIONS (1)
#define MICROPY_PY_STRUCT (1)

// Enable REPL functionality for interactive use
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_EVENT_DRIVEN (1)
#define MICROPY_PY_SYS_PS1_PS2 (1)

// Disable built-in asyncio to avoid conflicts (use frozen version)
#define MICROPY_PY_ASYNCIO (0)
#define MICROPY_PY_THREADING (0)
#define MICROPY_PY_SOCKET (0)

// Enable JavaScript integration hooks for browser/Node runtime
#define MICROPY_VARIANT_ENABLE_JS_HOOK (1)
