// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#pragma once

#include "shared_memory_config.h"

#if HAS_SHARED_HEAP || HAS_SHARED_VFS

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// SharedArrayBuffer integration for WebAssembly
// Extends supervisor/shared/stack.h with persistent heap management

// JavaScript SharedArrayBuffer integration
typedef struct {
    void *shared_heap_base;      // Base address of SharedArrayBuffer heap
    size_t shared_heap_size;     // Total size of shared heap
    size_t shared_heap_used;     // Currently allocated bytes
    bool shared_heap_enabled;    // Whether SharedArrayBuffer is active
    
    void *vfs_buffer_base;       // Base address of VFS SharedArrayBuffer  
    size_t vfs_buffer_size;      // Size of VFS buffer
    bool vfs_buffer_enabled;     // Whether VFS buffer is active
} js_shared_memory_t;

extern js_shared_memory_t js_shared_memory;

// SharedArrayBuffer management functions
bool js_shared_heap_init(void *heap_buffer, size_t heap_size);
bool js_shared_vfs_init(void *vfs_buffer, size_t vfs_size);
void *js_shared_heap_alloc(size_t size);
void js_shared_heap_free(void *ptr);
bool js_shared_heap_check(void);

// Object persistence across JavaScript calls
void js_shared_preserve_object(void *obj, size_t size);
void *js_shared_restore_object(const char *key);
void js_shared_cleanup_objects(void);

// Integration with CircuitPython GC
void js_shared_gc_collect(void);
bool js_shared_gc_is_locked(void);

// JavaScript API functions
void mp_js_register_shared_heap(uint32_t *heap_buffer_ref, size_t heap_size);
void mp_js_register_shared_vfs(uint32_t *vfs_buffer_ref, size_t vfs_size);

#endif // HAS_SHARED_HEAP || HAS_SHARED_VFS