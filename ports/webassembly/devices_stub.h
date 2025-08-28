// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2023 CircuitPython WebAssembly Contributors
//
// SPDX-License-Identifier: MIT

#ifndef MICROPY_INCLUDED_SUPERVISOR_SHARED_EXTERNAL_FLASH_DEVICES_H
#define MICROPY_INCLUDED_SUPERVISOR_SHARED_EXTERNAL_FLASH_DEVICES_H

// CircuitPython WebAssembly Port - External Flash Devices
//
// CURRENT STATE: No external flash devices defined for WebAssembly
// The WebAssembly port currently runs without external flash storage,
// using only the virtual filesystem provided by Emscripten.
//
// FUTURE ENHANCEMENT POSSIBILITIES:
// This file could be extended to define "virtual flash devices" that
// map CircuitPython's external flash API to browser storage APIs:
//
// 1. INDEXEDDB INTEGRATION:
//    - Define virtual device with IndexedDB as backing store
//    - Async storage operations via Emscripten's async support  
//    - Persistent storage across browser sessions
//    - Large capacity (limited by browser quota)
//
// 2. LOCALSTORAGE/SESSIONSTORAGE:
//    - Smaller capacity synchronous storage
//    - Simple key-value interface
//    - Good for configuration and small data
//
// 3. OPFS (ORIGIN PRIVATE FILE SYSTEM):
//    - Native file system API when available
//    - Better performance than IndexedDB for large files
//    - Stream-based operations
//
// 4. WEBASSEMBLY MEMORY PERSISTENCE:
//    - Memory-mapped storage using browser's persistent storage
//    - Direct byte-level access for compatibility
//
// Example future device definition:
/*
#define BROWSER_INDEXEDDB_DEVICE { \
    .total_size = (64 * 1024 * 1024),  // 64MB virtual capacity \
    .start_up_time_us = 1000,          // IndexedDB init time \
    .manufacturer_id = 0xWEB,          // Virtual manufacturer \
    .memory_type = 0xDB,               // Database storage type \
    .capacity = 0x1A,                  // 64MB capacity code \
    .max_clock_speed_mhz = 0,          // N/A for async storage \
    .quad_enable_bit_mask = 0,         // N/A for browser storage \
    .has_sector_protection = false,    // Browser handles security \
    .supports_fast_read = true,        // Async reads are "fast" \
    .supports_qspi = true,             // Parallel async operations \
    .supports_qspi_writes = true,      // Async write operations \
}
*/
//
// Implementation would require:
// - JavaScript storage backend (indexedDB, localStorage, etc.)
// - Emscripten async/await integration  
// - CircuitPython filesystem compatibility layer
// - Browser storage quota management
//
// This would enable CircuitPython WebAssembly to have persistent storage
// that survives browser restarts while maintaining compatibility with
// existing CircuitPython filesystem code.

// For now, no external flash devices are defined
// EXTERNAL_FLASH_DEVICES macro will expand to empty, creating:
// static const external_flash_device possible_devices[] = {};

#endif  // MICROPY_INCLUDED_SUPERVISOR_SHARED_EXTERNAL_FLASH_DEVICES_H