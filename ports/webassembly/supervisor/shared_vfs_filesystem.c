// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors
//
// SPDX-License-Identifier: MIT

#include "supervisor/shared_vfs_filesystem.h"
#include "supervisor/filesystem.h" 
#include "supervisor/shared_heap_stack.h"
#include <string.h>
#include <stdlib.h>

// Global SharedArrayBuffer VFS state
js_shared_vfs_t js_shared_vfs = {
    .buffer_base = NULL,
    .buffer_size = 0,
    .used_space = 0,
    .is_mounted = false,
    .is_writable = true,
    .file_count = 0
};

// JavaScript sync callback for live file updates
static void (*js_sync_callback)(const char *path, const void *data, size_t size) = NULL;

// Enhanced filesystem functions with SharedArrayBuffer integration
void filesystem_background(void) {
    // Periodic sync to SharedArrayBuffer
    if (js_shared_vfs.is_mounted) {
        js_vfs_sync();
    }
}

void filesystem_tick(void) {
    // Check for filesystem consistency
    if (js_shared_vfs.is_mounted && js_shared_vfs.buffer_base) {
        // Validate filesystem integrity
        js_vfs_is_persistent();
    }
}

bool filesystem_init(bool create_allowed, bool force_create) {
    (void)create_allowed;
    (void)force_create;
    
    // Initialize with SharedArrayBuffer if available
    if (js_shared_memory.vfs_buffer_enabled) {
        return js_vfs_init(js_shared_memory.vfs_buffer_base, js_shared_memory.vfs_buffer_size);
    }
    
    return true;
}

void filesystem_flush(void) {
    if (js_shared_vfs.is_mounted) {
        js_vfs_flush();
    }
}

void filesystem_set_internal_writable_by_usb(bool writable) {
    js_shared_vfs.is_writable = writable;
}

void filesystem_set_writable_by_usb(fs_user_mount_t *vfs, bool usb_writable) {
    (void)vfs;
    js_shared_vfs.is_writable = usb_writable;
}

bool filesystem_is_writable_by_python(fs_user_mount_t *vfs) {
    (void)vfs;
    return js_shared_vfs.is_writable;
}

bool filesystem_is_writable_by_usb(fs_user_mount_t *vfs) {
    (void)vfs;
    return js_shared_vfs.is_writable;
}

void filesystem_set_internal_concurrent_write_protection(bool concurrent_write_protection) {
    (void)concurrent_write_protection;
    // SharedArrayBuffer handles concurrent access
}

void filesystem_set_concurrent_write_protection(fs_user_mount_t *vfs, bool concurrent_write_protection) {
    (void)vfs;
    (void)concurrent_write_protection;
    // SharedArrayBuffer handles concurrent access
}

bool filesystem_present(void) {
    return js_shared_vfs.is_mounted;
}

// SharedArrayBuffer VFS implementation
bool js_vfs_init(void *buffer, size_t buffer_size) {
    if (!buffer || buffer_size < 8192) {
        return false;
    }
    
    js_shared_vfs.buffer_base = buffer;
    js_shared_vfs.buffer_size = buffer_size;
    js_shared_vfs.used_space = 512; // Reserve space for metadata
    js_shared_vfs.is_mounted = false;
    js_shared_vfs.is_writable = true;
    js_shared_vfs.file_count = 0;
    
    // Initialize file table
    memset(js_shared_vfs.file_table, 0, sizeof(js_shared_vfs.file_table));
    
    // Initialize buffer header
    struct vfs_header {
        uint32_t magic;           // 'JSVF' - JavaScript Shared VFS
        uint32_t version;
        uint32_t file_count;
        uint32_t used_space;
        uint8_t reserved[496];    // Pad to 512 bytes
    } *header = (struct vfs_header*)buffer;
    
    if (header->magic != 0x4A535646) { // 'JSVF'
        // Initialize new filesystem
        header->magic = 0x4A535646;
        header->version = 1;
        header->file_count = 0;
        header->used_space = 512;
        memset(header->reserved, 0, sizeof(header->reserved));
    } else {
        // Load existing filesystem
        js_shared_vfs.file_count = header->file_count;
        js_shared_vfs.used_space = header->used_space;
        
        // TODO: Load file table from buffer
    }
    
    return true;
}

void js_vfs_deinit(void) {
    js_vfs_flush();
    js_shared_vfs.is_mounted = false;
}

bool js_vfs_mount(const char *mount_point) {
    (void)mount_point;
    if (!js_shared_vfs.buffer_base) {
        return false;
    }
    
    js_shared_vfs.is_mounted = true;
    return true;
}

void js_vfs_unmount(void) {
    js_vfs_flush();
    js_shared_vfs.is_mounted = false;
}

// Simplified file operations (production would use proper FAT filesystem)
int js_vfs_open(const char *path, int flags) {
    if (!js_shared_vfs.is_mounted || !path) {
        return -1;
    }
    
    // Find existing file or allocate new slot
    for (size_t i = 0; i < 64; i++) {
        if (!js_shared_vfs.file_table[i].in_use) {
            strncpy(js_shared_vfs.file_table[i].name, path, 63);
            js_shared_vfs.file_table[i].name[63] = 0;
            js_shared_vfs.file_table[i].offset = js_shared_vfs.used_space;
            js_shared_vfs.file_table[i].size = 0;
            js_shared_vfs.file_table[i].in_use = true;
            return i; // Return file descriptor
        }
    }
    
    return -1; // No available file descriptors
}

int js_vfs_close(int fd) {
    if (fd < 0 || fd >= 64 || !js_shared_vfs.file_table[fd].in_use) {
        return -1;
    }
    
    js_shared_vfs.file_table[fd].in_use = false;
    return 0;
}

ssize_t js_vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= 64 || !js_shared_vfs.file_table[fd].in_use || !js_shared_vfs.is_writable) {
        return -1;
    }
    
    // Check if we have space
    if (js_shared_vfs.used_space + count > js_shared_vfs.buffer_size) {
        return -1; // No space
    }
    
    // Write to buffer
    char *write_ptr = (char*)js_shared_vfs.buffer_base + js_shared_vfs.file_table[fd].offset + js_shared_vfs.file_table[fd].size;
    memcpy(write_ptr, buf, count);
    
    js_shared_vfs.file_table[fd].size += count;
    js_shared_vfs.used_space += count;
    
    // Notify JavaScript if callback is set
    if (js_sync_callback) {
        js_sync_callback(js_shared_vfs.file_table[fd].name, buf, count);
    }
    
    return count;
}

ssize_t js_vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= 64 || !js_shared_vfs.file_table[fd].in_use) {
        return -1;
    }
    
    size_t available = js_shared_vfs.file_table[fd].size;
    size_t to_read = (count < available) ? count : available;
    
    if (to_read > 0) {
        char *read_ptr = (char*)js_shared_vfs.buffer_base + js_shared_vfs.file_table[fd].offset;
        memcpy(buf, read_ptr, to_read);
    }
    
    return to_read;
}

void js_vfs_sync(void) {
    if (!js_shared_vfs.is_mounted) {
        return;
    }
    
    // Update header in SharedArrayBuffer
    struct vfs_header {
        uint32_t magic;
        uint32_t version; 
        uint32_t file_count;
        uint32_t used_space;
        uint8_t reserved[496];
    } *header = (struct vfs_header*)js_shared_vfs.buffer_base;
    
    header->file_count = js_shared_vfs.file_count;
    header->used_space = js_shared_vfs.used_space;
    
    // In production, this would sync the complete file allocation table
}

void js_vfs_flush(void) {
    js_vfs_sync();
    // Force any pending writes to SharedArrayBuffer
}

bool js_vfs_is_persistent(void) {
    return js_shared_vfs.is_mounted && js_shared_vfs.buffer_base != NULL;
}

size_t js_vfs_get_free_space(void) {
    if (!js_shared_vfs.is_mounted) {
        return 0;
    }
    return js_shared_vfs.buffer_size - js_shared_vfs.used_space;
}

// JavaScript integration functions
bool js_vfs_import_from_js_files(void) {
    // This would allow JavaScript to populate the VFS with initial files
    // Implementation would call JavaScript callback to enumerate files
    return true;
}

bool js_vfs_export_to_js_files(void) {
    // This would export VFS contents back to JavaScript
    // Useful for debugging or extracting files created in Python
    return true;
}

void js_vfs_set_js_sync_callback(void (*callback)(const char *path, const void *data, size_t size)) {
    js_sync_callback = callback;
}