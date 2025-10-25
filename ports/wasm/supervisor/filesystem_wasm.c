// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// WASM-specific filesystem implementation for CircuitPython
// Wraps the JavaScript filesystem.js functionality that provides
// IndexedDB persistence via Emscripten's VFS.

#include <emscripten/emscripten.h>
#include <stdbool.h>
#include <string.h>

#include "supervisor/filesystem.h"
#include "py/runtime.h"

// Global flag that can be set by other parts of the system to request a flush
volatile bool filesystem_flush_requested = false;

// Static VFS mount point for CIRCUITPY (Emscripten VFS managed by JavaScript)
// In WASM, we use Emscripten's VFS which is already initialized by JavaScript
static fs_user_mount_t vfs_circuitpy = {
    .base = { &mp_fat_vfs_type },
    .blockdev = { 0 },
    .fatfs = { 0 },
    .lock_count = 0,
};

bool filesystem_init(bool create_allowed, bool force_create) {
    // In WASM port, the filesystem is already initialized by JavaScript
    // via filesystem.js which calls Module.FS operations.
    //
    // The JavaScript side (api.js) handles:
    // - Creating IndexedDB database
    // - Mounting IDBFS at /circuitpy or /
    // - Syncing files from IndexedDB to VFS (syncToVFS)
    //
    // We just need to return true to indicate filesystem is ready.

    (void)create_allowed;
    (void)force_create;

    return true;
}

void filesystem_flush(void) {
    // Tell JavaScript to sync the VFS to IndexedDB
    // This is called when Python code wants to ensure files are persisted

    emscripten_run_script(
        "if (typeof Module.filesystem !== 'undefined' && "
        "    typeof Module.filesystem.syncFromVFS === 'function') { "
        "  try { "
        "    const rootFiles = FS.readdir('/').filter(f => f !== '.' && f !== '..').map(f => '/' + f); "
        "    Module.filesystem.syncFromVFS(Module, rootFiles).catch(err => { "
        "      console.error('Filesystem flush failed:', err); "
        "    }); "
        "  } catch (e) { "
        "    console.error('Error reading directory for flush:', e); "
        "  } "
        "}"
    );

    filesystem_flush_requested = false;
}

void filesystem_background(void) {
    // Background flush processing
    // In WASM, JavaScript handles async I/O, so we don't need to do much here

    if (filesystem_flush_requested) {
        filesystem_flush();
    }
}

void filesystem_tick(void) {
    // Periodic filesystem operations (called from tick interrupt)
    // For WASM, we don't need periodic flushes since JavaScript handles it
    // But we can honor explicit flush requests

    // No-op for now - JavaScript manages async persistence
}

bool filesystem_present(void) {
    // Return true if filesystem is available
    // In WASM, the VFS is always present (managed by Emscripten)
    return true;
}

void filesystem_set_internal_writable_by_usb(bool usb_writable) {
    // WASM has no USB, so this is a no-op
    (void)usb_writable;
}

void filesystem_set_internal_concurrent_write_protection(bool concurrent_write_protection) {
    // WASM runs single-threaded in the main JavaScript thread
    // No concurrent access protection needed
    (void)concurrent_write_protection;
}

void filesystem_set_writable_by_usb(fs_user_mount_t *vfs, bool usb_writable) {
    // WASM has no USB, so this is a no-op
    (void)vfs;
    (void)usb_writable;
}

void filesystem_set_concurrent_write_protection(fs_user_mount_t *vfs, bool concurrent_write_protection) {
    // WASM runs single-threaded, no concurrent protection needed
    (void)vfs;
    (void)concurrent_write_protection;
}

void filesystem_set_ignore_write_protection(fs_user_mount_t *vfs, bool ignore_write_protection) {
    // No write protection in WASM
    (void)vfs;
    (void)ignore_write_protection;
}

bool filesystem_is_writable_by_python(fs_user_mount_t *vfs) {
    // In WASM, Python always has write access to the filesystem
    (void)vfs;
    return true;
}

bool filesystem_is_writable_by_usb(fs_user_mount_t *vfs) {
    // WASM has no USB
    (void)vfs;
    return false;
}

fs_user_mount_t *filesystem_circuitpy(void) {
    // Return the CIRCUITPY mount point
    // In WASM, we use a static VFS mount
    return &vfs_circuitpy;
}

fs_user_mount_t *filesystem_for_path(const char *path_in, const char **path_under_mount) {
    // Find which VFS mount point contains the given path
    // In WASM, we only have one mount point at /

    if (path_in == NULL) {
        return NULL;
    }

    // For WASM, all paths are under the root mount
    if (path_under_mount != NULL) {
        *path_under_mount = path_in;
    }

    return &vfs_circuitpy;
}

bool filesystem_native_fatfs(fs_user_mount_t *fs_mount) {
    // Check if this is a native FAT filesystem
    // In WASM, we use Emscripten's VFS which may or may not be FAT-based
    // For simplicity, return false since we're using IDBFS/MEMFS
    (void)fs_mount;
    return false;
}

// Locking functions for concurrent access
// In WASM, we run single-threaded, but we still provide the API

bool filesystem_lock(fs_user_mount_t *fs_mount) {
    // Acquire filesystem lock
    // In single-threaded WASM, this is a no-op but we track lock count
    if (fs_mount == NULL) {
        return false;
    }

    fs_mount->lock_count++;
    return true;
}

void filesystem_unlock(fs_user_mount_t *fs_mount) {
    // Release filesystem lock
    if (fs_mount == NULL) {
        return;
    }

    if (fs_mount->lock_count > 0) {
        fs_mount->lock_count--;
    }
}

bool blockdev_lock(fs_user_mount_t *fs_mount) {
    // Acquire block device lock
    // In WASM, this is effectively the same as filesystem_lock
    return filesystem_lock(fs_mount);
}

void blockdev_unlock(fs_user_mount_t *fs_mount) {
    // Release block device lock
    filesystem_unlock(fs_mount);
}
