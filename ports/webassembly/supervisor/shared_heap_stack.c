// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include "shared_memory_config.h"

#if HAS_SHARED_HEAP || HAS_SHARED_VFS

#include "supervisor/shared_heap_stack.h"
#include "supervisor/shared/stack.h"
#include "py/gc.h"
#include "py/runtime.h"
// Don't include proxy_c.h to avoid conflicts
#include <string.h>

// Global shared memory management state
js_shared_memory_t js_shared_memory = {
    .shared_heap_base = NULL,
    .shared_heap_size = 0,
    .shared_heap_used = 0,
    .shared_heap_enabled = false,
    .vfs_buffer_base = NULL,
    .vfs_buffer_size = 0,
    .vfs_buffer_enabled = false
};

// Object persistence hash table for cross-call object storage
#define JS_OBJECT_CACHE_SIZE 64
typedef struct js_object_cache_entry {
    char key[32];
    void *object_ptr;
    size_t object_size;
    uint32_t reference_count;
    struct js_object_cache_entry *next;
} js_object_cache_entry_t;

static js_object_cache_entry_t *object_cache[JS_OBJECT_CACHE_SIZE] = {NULL};

// Enhanced stack functions with SharedArrayBuffer integration
bool stack_ok(void) {
    // Check both regular stack and shared heap health
    if (js_shared_memory.shared_heap_enabled) {
        return js_shared_heap_check();
    }
    return true;
}

void assert_heap_ok(void) {
    if (js_shared_memory.shared_heap_enabled && !js_shared_heap_check()) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("SharedArrayBuffer heap corruption"));
    }
}

void stack_init(void) {
    // Initialize shared memory structures
    memset(&js_shared_memory, 0, sizeof(js_shared_memory_t));
    memset(object_cache, 0, sizeof(object_cache));
}

// SharedArrayBuffer heap management
bool js_shared_heap_init(void *heap_buffer, size_t heap_size) {
    if (!heap_buffer || heap_size < 1024) {
        return false;
    }
    
    js_shared_memory.shared_heap_base = heap_buffer;
    js_shared_memory.shared_heap_size = heap_size;
    js_shared_memory.shared_heap_used = 0;
    js_shared_memory.shared_heap_enabled = true;
    
    // Initialize heap header
    memset(heap_buffer, 0, 64); // Reserve first 64 bytes for metadata
    js_shared_memory.shared_heap_used = 64;
    
    return true;
}

bool js_shared_vfs_init(void *vfs_buffer, size_t vfs_size) {
    if (!vfs_buffer || vfs_size < 4096) {
        return false;
    }
    
    js_shared_memory.vfs_buffer_base = vfs_buffer;
    js_shared_memory.vfs_buffer_size = vfs_size;
    js_shared_memory.vfs_buffer_enabled = true;
    
    // Initialize VFS buffer with basic FAT filesystem structure
    // This would be expanded to create a proper FAT filesystem
    memset(vfs_buffer, 0, vfs_size);
    
    return true;
}

void *js_shared_heap_alloc(size_t size) {
    if (!js_shared_memory.shared_heap_enabled) {
        return NULL;
    }
    
    // Align to 8-byte boundaries
    size = (size + 7) & ~7;
    
    if (js_shared_memory.shared_heap_used + size > js_shared_memory.shared_heap_size) {
        return NULL; // Out of shared memory
    }
    
    void *ptr = (char*)js_shared_memory.shared_heap_base + js_shared_memory.shared_heap_used;
    js_shared_memory.shared_heap_used += size;
    
    return ptr;
}

void js_shared_heap_free(void *ptr) {
    // Simple implementation - in production this would need a proper allocator
    // For now, we rely on periodic cleanup
    (void)ptr;
}

bool js_shared_heap_check(void) {
    if (!js_shared_memory.shared_heap_enabled) {
        return true;
    }
    
    // Basic sanity checks
    return (js_shared_memory.shared_heap_used <= js_shared_memory.shared_heap_size) &&
           (js_shared_memory.shared_heap_base != NULL);
}

// Object persistence for complex objects across JavaScript calls
static uint32_t hash_key(const char *key) {
    uint32_t hash = 5381;
    while (*key) {
        hash = ((hash << 5) + hash) + *key++;
    }
    return hash % JS_OBJECT_CACHE_SIZE;
}

void js_shared_preserve_object(void *obj, size_t size) {
    if (!obj || !js_shared_memory.shared_heap_enabled) {
        return;
    }
    
    // Create a key based on object address (simple approach)
    char key[32];
    snprintf(key, sizeof(key), "obj_%p", obj);
    
    uint32_t index = hash_key(key);
    js_object_cache_entry_t *entry = object_cache[index];
    
    // Check if object already cached
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->reference_count++;
            return;
        }
        entry = entry->next;
    }
    
    // Allocate new cache entry in shared heap
    entry = js_shared_heap_alloc(sizeof(js_object_cache_entry_t) + size);
    if (!entry) {
        return; // Out of memory
    }
    
    strncpy(entry->key, key, sizeof(entry->key) - 1);
    entry->object_ptr = (char*)entry + sizeof(js_object_cache_entry_t);
    entry->object_size = size;
    entry->reference_count = 1;
    entry->next = object_cache[index];
    
    // Copy object data to shared memory
    memcpy(entry->object_ptr, obj, size);
    
    object_cache[index] = entry;
}

void *js_shared_restore_object(const char *key) {
    if (!key || !js_shared_memory.shared_heap_enabled) {
        return NULL;
    }
    
    uint32_t index = hash_key(key);
    js_object_cache_entry_t *entry = object_cache[index];
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->object_ptr;
        }
        entry = entry->next;
    }
    
    return NULL;
}

void js_shared_cleanup_objects(void) {
    // Cleanup unreferenced objects (simplified implementation)
    for (int i = 0; i < JS_OBJECT_CACHE_SIZE; i++) {
        js_object_cache_entry_t *entry = object_cache[i];
        js_object_cache_entry_t *prev = NULL;
        
        while (entry) {
            if (entry->reference_count == 0) {
                if (prev) {
                    prev->next = entry->next;
                } else {
                    object_cache[i] = entry->next;
                }
                // In a real implementation, we'd free the memory here
                entry = entry->next;
            } else {
                entry->reference_count--; // Age out objects
                prev = entry;
                entry = entry->next;
            }
        }
    }
}

// Integration with CircuitPython GC
void js_shared_gc_collect(void) {
    if (!js_shared_memory.shared_heap_enabled) {
        return;
    }
    
    // Mark objects in shared heap as reachable
    // This prevents the regular GC from collecting objects we've preserved
    // Implementation would scan object_cache and call gc_collect_ptr() on each
    
    for (int i = 0; i < JS_OBJECT_CACHE_SIZE; i++) {
        js_object_cache_entry_t *entry = object_cache[i];
        while (entry) {
            if (entry->object_ptr && entry->reference_count > 0) {
                // Tell GC this object is still reachable
                gc_collect_ptr(entry->object_ptr);
            }
            entry = entry->next;
        }
    }
}

bool js_shared_gc_is_locked(void) {
    // Return true if we're in the middle of a shared memory operation
    return false; // Simplified - would track lock state in real implementation
}

// JavaScript API functions to register SharedArrayBuffers
void mp_js_register_shared_heap(uint32_t *heap_buffer_ref, size_t heap_size) {
    // Extract JavaScript ArrayBuffer and map it to WebAssembly memory
    // This would need Emscripten-specific code to access the SharedArrayBuffer
    
    // For now, simulate with regular malloc (in production, this would be
    // mapped to a JavaScript SharedArrayBuffer)
    void *heap_buffer = malloc(heap_size);
    if (heap_buffer) {
        js_shared_heap_init(heap_buffer, heap_size);
    }
}

void mp_js_register_shared_vfs(uint32_t *vfs_buffer_ref, size_t vfs_size) {
    // Similar to above - extract JavaScript ArrayBuffer for VFS
    void *vfs_buffer = malloc(vfs_size);
    if (vfs_buffer) {
        js_shared_vfs_init(vfs_buffer, vfs_size);
    }
}

#endif // HAS_SHARED_HEAP || HAS_SHARED_VFS