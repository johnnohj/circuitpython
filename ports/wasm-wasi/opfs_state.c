/*
 * opfs_state.c — State checkpoint/restore via POSIX file I/O
 *
 * Saves GC heap, mp_state_ctx, and pystack to files. Loads them back
 * at the same WASM linear memory addresses for pointer stability.
 *
 * Based on the checkpoint/restore pattern from ports/wasm-dist/memfs_state.c.
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "py/gc.h"
#include "py/mpstate.h"
#include "py/pystack.h"
#include "opfs_state.h"

// ---- Helpers ----

// Build a path like "/state/heap" from dir="/state" and name="heap"
static void make_path(char *buf, size_t buflen, const char *dir, const char *name) {
    snprintf(buf, buflen, "%s/%s", dir, name);
}

// Write a state file: header + raw data
static int save_file(const char *path, const void *data, size_t data_size, uintptr_t base_addr) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return -1;
    }

    opfs_state_header_t hdr = {
        .magic = OPFS_STATE_MAGIC,
        .version = OPFS_STATE_VERSION,
        .data_size = (uint32_t)data_size,
        .wasm_base_addr = (uint32_t)base_addr,
    };

    write(fd, &hdr, sizeof(hdr));
    write(fd, data, data_size);
    close(fd);
    return 0;
}

// Load a state file: verify header, read raw data into the given buffer
static int load_file(const char *path, void *data, size_t data_size, uintptr_t expected_addr) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    opfs_state_header_t hdr;
    ssize_t n = read(fd, &hdr, sizeof(hdr));
    if (n != sizeof(hdr) || hdr.magic != OPFS_STATE_MAGIC) {
        close(fd);
        return -1;
    }

    // Verify pointer stability: data must load at the same address
    if (hdr.wasm_base_addr != (uint32_t)expected_addr) {
        fprintf(stderr, "opfs_state: address mismatch for %s (expected %u, got %u)\n",
                path, (unsigned)expected_addr, (unsigned)hdr.wasm_base_addr);
        close(fd);
        return -2;
    }

    // Read the payload
    size_t to_read = hdr.data_size < data_size ? hdr.data_size : data_size;
    n = read(fd, data, to_read);
    close(fd);

    return (n == (ssize_t)to_read) ? 0 : -1;
}

// ---- GC Heap ----

int opfs_save_heap(const char *state_dir) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "heap");

    byte *save_start = MP_STATE_MEM(area.gc_alloc_table_start);
    byte *save_end = MP_STATE_MEM(area.gc_pool_end);
    size_t save_size = (size_t)(save_end - save_start);

    return save_file(path, save_start, save_size, (uintptr_t)save_start);
}

int opfs_load_heap(const char *state_dir) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "heap");

    byte *save_start = MP_STATE_MEM(area.gc_alloc_table_start);
    byte *save_end = MP_STATE_MEM(area.gc_pool_end);
    size_t save_size = (size_t)(save_end - save_start);

    return load_file(path, save_start, save_size, (uintptr_t)save_start);
}

// ---- VM State (mp_state_ctx) ----

int opfs_save_vm(const char *state_dir) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "vm");

    return save_file(path, &mp_state_ctx, sizeof(mp_state_ctx), (uintptr_t)&mp_state_ctx);
}

int opfs_load_vm(const char *state_dir) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "vm");

    return load_file(path, &mp_state_ctx, sizeof(mp_state_ctx), (uintptr_t)&mp_state_ctx);
}

// ---- Pystack ----

int opfs_save_pystack(const char *state_dir) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "pystack");

    #if MICROPY_ENABLE_PYSTACK
    byte *start = (byte *)MP_STATE_THREAD(pystack_start);
    byte *cur = (byte *)MP_STATE_THREAD(pystack_cur);
    size_t used = (size_t)(cur - start);

    return save_file(path, start, used, (uintptr_t)start);
    #else
    return 0;  // no pystack to save
    #endif
}

int opfs_load_pystack(const char *state_dir) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "pystack");

    #if MICROPY_ENABLE_PYSTACK
    byte *start = (byte *)MP_STATE_THREAD(pystack_start);
    byte *end = (byte *)MP_STATE_THREAD(pystack_end);
    size_t capacity = (size_t)(end - start);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    opfs_state_header_t hdr;
    ssize_t n = read(fd, &hdr, sizeof(hdr));
    if (n != sizeof(hdr) || hdr.magic != OPFS_STATE_MAGIC) {
        close(fd);
        return -1;
    }

    if (hdr.wasm_base_addr != (uint32_t)(uintptr_t)start) {
        close(fd);
        return -2;
    }

    size_t to_read = hdr.data_size < capacity ? hdr.data_size : capacity;
    n = read(fd, start, to_read);
    close(fd);

    if (n != (ssize_t)to_read) {
        return -1;
    }

    // Restore pystack_cur to point past the loaded data
    MP_STATE_THREAD(pystack_cur) = start + to_read;
    return 0;
    #else
    return 0;
    #endif
}

// ---- Execution Metadata ----

int opfs_save_exec(const char *state_dir, const opfs_exec_meta_t *meta) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "exec");

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return -1;
    }
    write(fd, meta, sizeof(*meta));
    close(fd);
    return 0;
}

int opfs_load_exec(const char *state_dir, opfs_exec_meta_t *meta) {
    char path[128];
    make_path(path, sizeof(path), state_dir, "exec");

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        memset(meta, 0, sizeof(*meta));
        return -1;
    }
    ssize_t n = read(fd, meta, sizeof(*meta));
    close(fd);

    if (n != sizeof(*meta) || meta->magic != OPFS_STATE_MAGIC) {
        memset(meta, 0, sizeof(*meta));
        return -1;
    }
    return 0;
}
