/*
 * Conditional SharedArrayBuffer Integration for Unport
 * 
 * This header provides build-time configuration for SharedArrayBuffer
 * integration while maintaining compatibility with the original proxy system.
 */

#pragma once

// Build-time configuration flags
#ifndef CIRCUITPY_SHARED_ARRAY_BUFFER
#define CIRCUITPY_SHARED_ARRAY_BUFFER 0  // Disabled by default for proxy compatibility
#endif

#ifndef CIRCUITPY_SHARED_VFS_BUFFER  
#define CIRCUITPY_SHARED_VFS_BUFFER 0    // VFS SharedArrayBuffer integration
#endif

// Memory domain separation
#define MEMORY_DOMAIN_RUNTIME  0    // Proxy system and Python runtime
#define MEMORY_DOMAIN_VFS      1    // File system operations  
#define MEMORY_DOMAIN_PERSIST  2    // Persistent cross-call storage

// Feature flags for different use cases
#if CIRCUITPY_SHARED_ARRAY_BUFFER
    #define HAS_SHARED_HEAP 1
    #define HAS_OBJECT_PERSISTENCE 1
#else
    #define HAS_SHARED_HEAP 0
    #define HAS_OBJECT_PERSISTENCE 0
#endif

#if CIRCUITPY_SHARED_VFS_BUFFER
    #define HAS_SHARED_VFS 1
#else  
    #define HAS_SHARED_VFS 0
#endif

// Conditional inclusion guards
#if HAS_SHARED_HEAP || HAS_SHARED_VFS
    #include "supervisor/shared_heap_stack.h"
#endif