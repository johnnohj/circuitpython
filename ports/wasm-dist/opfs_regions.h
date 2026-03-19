/*
 * opfs_regions.h — OPFS-backed shared memory regions
 *
 * Named binary regions accessible from any worker via the Origin Private
 * File System (OPFS).  In browsers that support OPFS, each region is backed
 * by a persistent file with synchronous I/O (createSyncAccessHandle).
 * In Node.js, falls back to in-memory buffers (same API, no persistence).
 *
 * Regions:
 *   REGISTERS  — 256 × 16-bit pin/sensor values (512 bytes)
 *   SENSORS    — I2C/SPI mailbox: request + response (4096 bytes)
 *   EVENTS     — binary event ring buffer (8192 bytes)
 *   FRAMEBUF   — display pixel data, variable size
 *   CONTROL    — flags, timestamps, worker IDs (64 bytes)
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

// Region IDs (must match JS-side REGION_* constants in libpyopfs.js)
#define OPFS_REGION_REGISTERS   0
#define OPFS_REGION_SENSORS     1
#define OPFS_REGION_EVENTS      2
#define OPFS_REGION_FRAMEBUF    3
#define OPFS_REGION_CONTROL     4
#define OPFS_NUM_REGIONS        5

// Default region sizes
#define OPFS_SIZE_REGISTERS     512     // 256 × uint16
#define OPFS_SIZE_SENSORS       4096
#define OPFS_SIZE_EVENTS        8192
#define OPFS_SIZE_FRAMEBUF      0       // allocated on demand
#define OPFS_SIZE_CONTROL       64

// ---- C API (implemented in libpyopfs.js as Emscripten library) ----

// Initialize the OPFS region system.  In browsers, opens OPFS handles.
// In Node.js, allocates in-memory buffers.  Safe to call multiple times.
void opfs_init(void);

// Read bytes from a region.
// Returns number of bytes read, or 0 on error.
int opfs_read(int region_id, uint32_t offset, void *buf, uint32_t len);

// Write bytes to a region.
// Returns number of bytes written, or 0 on error.
int opfs_write(int region_id, uint32_t offset, const void *buf, uint32_t len);

// Get the size of a region in bytes.
uint32_t opfs_region_size(int region_id);

// Flush a region to persistent storage (no-op for in-memory fallback).
void opfs_flush(int region_id);

// ---- Convenience: register file via OPFS ----
// These mirror the hw_reg_read/write API but route through OPFS regions
// so other workers can see the same register values.

static inline uint16_t opfs_reg_read(int addr) {
    uint16_t val = 0;
    opfs_read(OPFS_REGION_REGISTERS, addr * 2, &val, 2);
    return val;
}

static inline void opfs_reg_write(int addr, uint16_t val) {
    opfs_write(OPFS_REGION_REGISTERS, addr * 2, &val, 2);
}
