// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/supervisor/msgpack_fixup.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// msgpack_fixup.h — Pre-include to fix POSIX name clash.
//
// The upstream msgpack module defines `static void read(...)` and
// `static void write(...)` which clash with POSIX read(2)/write(2)
// from WASI sysroot's <unistd.h>.  Pre-define the include guard so
// the POSIX declarations are never seen.  The msgpack module uses
// MicroPython's stream protocol, not POSIX I/O.
#ifndef _UNISTD_H
#define _UNISTD_H
#endif
