// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Based on ports/wasm/chassis/memfs_imports.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// port/memfs_imports.h — WASM imports for MEMFS-in-linear-memory.
//
// JS (wasi.js) provides the "memfs" import module.  C calls
// memfs_register() to map a memory region to a MEMFS path.
// After registration, the MEMFS file IS the struct field —
// same bytes, no copy.
//
// JS instantiation:
//   const imports = {
//       memfs: {
//           register: (path_ptr, path_len, data_ptr, data_size) => { ... },
//       },
//   };
//
// Design refs:
//   design/wasm-layer.md  (MEMFS-in-linear-memory model)

#ifndef PORT_MEMFS_IMPORTS_H
#define PORT_MEMFS_IMPORTS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Raw WASM import — takes pointer + length for the path string.

__attribute__((import_module("memfs"), import_name("register")))
void __memfs_register(const char *path, uint32_t path_len,
                      void *data, uint32_t size);

// Convenience wrapper — C string path.

static inline void memfs_register(const char *path, void *data, uint32_t size) {
    __memfs_register(path, strlen(path), data, size);
}

#endif // PORT_MEMFS_IMPORTS_H
