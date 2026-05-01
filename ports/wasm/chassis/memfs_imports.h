/*
 * chassis/memfs_imports.h — WASM imports for MEMFS-in-linear-memory.
 *
 * These are functions provided by JS (wasi-memfs.js) and imported by
 * the WASM module.  They let C register memory regions as MEMFS files.
 *
 * The import module is "memfs" — JS provides these when instantiating
 * the WASM module:
 *
 *   const imports = {
 *       memfs: {
 *           register: (path_ptr, path_len, data_ptr, data_size) => { ... },
 *       },
 *       wasi_snapshot_preview1: { ... },
 *   };
 */

#ifndef CHASSIS_MEMFS_IMPORTS_H
#define CHASSIS_MEMFS_IMPORTS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Raw WASM import — takes pointer + length for the path string        */
/* ------------------------------------------------------------------ */

__attribute__((import_module("memfs"), import_name("register")))
void __memfs_register(const char *path, uint32_t path_len,
                      void *data, uint32_t size);

/* ------------------------------------------------------------------ */
/* Convenience wrapper — C string path                                 */
/* ------------------------------------------------------------------ */

static inline void memfs_register(const char *path, void *data, uint32_t size) {
    __memfs_register(path, strlen(path), data, size);
}

#endif /* CHASSIS_MEMFS_IMPORTS_H */
