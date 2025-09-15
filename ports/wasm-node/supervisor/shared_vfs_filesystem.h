// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython Contributors  
//
// SPDX-License-Identifier: MIT

#pragma once

#include "supervisor/filesystem.h"
#include <stdbool.h>
#include <stddef.h>

// SharedArrayBuffer-backed filesystem for WebAssembly
// Extends supervisor/filesystem.h with persistent storage

// VFS backed by JavaScript SharedArrayBuffer
typedef struct {
    void *buffer_base;           // Base of SharedArrayBuffer
    size_t buffer_size;          // Total buffer size
    size_t used_space;           // Currently used space
    bool is_mounted;             // Whether VFS is active
    bool is_writable;            // Whether writes are allowed
    
    // Simple file table (would be replaced with proper FAT in production)
    struct {
        char name[64];
        size_t offset;
        size_t size;
        bool in_use;
    } file_table[64];
    
    size_t file_count;
} js_shared_vfs_t;

extern js_shared_vfs_t js_shared_vfs;

// SharedArrayBuffer VFS functions
bool js_vfs_init(void *buffer, size_t buffer_size);
void js_vfs_deinit(void);
bool js_vfs_mount(const char *mount_point);
void js_vfs_unmount(void);

// File operations on SharedArrayBuffer
int js_vfs_open(const char *path, int flags);
int js_vfs_close(int fd);
ssize_t js_vfs_read(int fd, void *buf, size_t count);
ssize_t js_vfs_write(int fd, const void *buf, size_t count);
int js_vfs_unlink(const char *path);
int js_vfs_rename(const char *old_path, const char *new_path);
int js_vfs_stat(const char *path, void *stat_buf);

// Directory operations
int js_vfs_mkdir(const char *path);
int js_vfs_rmdir(const char *path);
void *js_vfs_opendir(const char *path);
int js_vfs_readdir(void *dir_handle, char *name_buf, size_t name_buf_size);
int js_vfs_closedir(void *dir_handle);

// Persistence and synchronization
void js_vfs_sync(void);        // Sync to SharedArrayBuffer
void js_vfs_flush(void);       // Flush pending writes
bool js_vfs_is_persistent(void);
size_t js_vfs_get_free_space(void);

// JavaScript integration
bool js_vfs_import_from_js_files(void);  // Import files from JavaScript FS object
bool js_vfs_export_to_js_files(void);    // Export files to JavaScript FS object
void js_vfs_set_js_sync_callback(void (*callback)(const char *path, const void *data, size_t size));