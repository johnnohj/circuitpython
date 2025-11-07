// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// OS module implementation for WASM port using storage peripheral

#include "common-hal/os/__init__.h"
#include "py/runtime.h"
#include "py/mphal.h"
#include "py/objstr.h"
#include <emscripten.h>
#include <string.h>
#include <stdlib.h>

// ========== Storage Peripheral Helper Functions ==========

// Check if storage peripheral is available
EM_JS(bool, has_storage_peripheral, (), {
    return Module.hasPeripheral && Module.hasPeripheral('storage');
});

// Get current working directory from storage peripheral
// Returns: JS string or null
EM_JS(char*, storage_getcwd, (), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.getcwd === 'function') {
            try {
                const cwd = storage.getcwd();
                if (typeof cwd === 'string') {
                    const len = lengthBytesUTF8(cwd) + 1;
                    const ptr = _malloc(len);
                    stringToUTF8(cwd, ptr, len);
                    return ptr;
                }
            } catch (e) {
                console.error('[OS] Storage getcwd error:', e);
            }
        }
    }
    return null;
});

// Change current directory via storage peripheral
// Returns: 0 on success, -1 if no peripheral, 1 if error
EM_JS(int, storage_chdir, (const char *path), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.chdir === 'function') {
            try {
                const pathStr = UTF8ToString(path);
                storage.chdir(pathStr);
                return 0;
            } catch (e) {
                console.error('[OS] Storage chdir error:', e);
                const msg = e.message || 'Directory change failed';
                const len = lengthBytesUTF8(msg) + 1;
                const ptr = _malloc(len);
                stringToUTF8(msg, ptr, len);
                Module._last_os_error = ptr;
                return 1;
            }
        }
    }
    return -1;
});

// List directory via storage peripheral
// Returns: JSON array string or null
EM_JS(char*, storage_listdir, (const char *path), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.listdir === 'function') {
            try {
                const pathStr = UTF8ToString(path);
                const resultPromiseOrValue = storage.listdir(pathStr);

                // Check if result is a Promise
                if (resultPromiseOrValue && typeof resultPromiseOrValue.then === 'function') {
                    console.warn('[OS] Storage listdir returned Promise, but sync operation expected. Using empty list.');
                    return null;
                }

                // Handle synchronous result
                const files = resultPromiseOrValue;
                if (Array.isArray(files)) {
                    const json = JSON.stringify(files);
                    const len = lengthBytesUTF8(json) + 1;
                    const ptr = _malloc(len);
                    stringToUTF8(json, ptr, len);
                    return ptr;
                }
            } catch (e) {
                console.error('[OS] Storage listdir error:', e);
                const msg = e.message || 'Failed to list directory';
                const len = lengthBytesUTF8(msg) + 1;
                const ptr = _malloc(len);
                stringToUTF8(msg, ptr, len);
                Module._last_os_error = ptr;
                return null;
            }
        }
    }
    return null;
});

// Create directory via storage peripheral
// Returns: 0 on success, -1 if no peripheral, 1 if error
EM_JS(int, storage_mkdir, (const char *path), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.mkdir === 'function') {
            try {
                const pathStr = UTF8ToString(path);
                const resultPromiseOrValue = storage.mkdir(pathStr);

                // Check if result is a Promise
                if (resultPromiseOrValue && typeof resultPromiseOrValue.then === 'function') {
                    console.warn('[OS] Storage mkdir returned Promise, but sync operation expected.');
                    return 1;
                }

                return 0;
            } catch (e) {
                console.error('[OS] Storage mkdir error:', e);
                const msg = e.message || 'Failed to create directory';
                const len = lengthBytesUTF8(msg) + 1;
                const ptr = _malloc(len);
                stringToUTF8(msg, ptr, len);
                Module._last_os_error = ptr;
                return 1;
            }
        }
    }
    return -1;
});

// Remove file via storage peripheral
// Returns: 0 on success, -1 if no peripheral, 1 if error
EM_JS(int, storage_remove, (const char *path), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.deleteFile === 'function') {
            try {
                const pathStr = UTF8ToString(path);
                const resultPromiseOrValue = storage.deleteFile(pathStr);

                // Check if result is a Promise
                if (resultPromiseOrValue && typeof resultPromiseOrValue.then === 'function') {
                    console.warn('[OS] Storage deleteFile returned Promise, but sync operation expected.');
                    return 1;
                }

                return 0;
            } catch (e) {
                console.error('[OS] Storage deleteFile error:', e);
                const msg = e.message || 'Failed to delete file';
                const len = lengthBytesUTF8(msg) + 1;
                const ptr = _malloc(len);
                stringToUTF8(msg, ptr, len);
                Module._last_os_error = ptr;
                return 1;
            }
        }
    }
    return -1;
});

// Remove directory via storage peripheral
// Returns: 0 on success, -1 if no peripheral, 1 if error
EM_JS(int, storage_rmdir, (const char *path), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.rmdir === 'function') {
            try {
                const pathStr = UTF8ToString(path);
                const resultPromiseOrValue = storage.rmdir(pathStr);

                // Check if result is a Promise
                if (resultPromiseOrValue && typeof resultPromiseOrValue.then === 'function') {
                    console.warn('[OS] Storage rmdir returned Promise, but sync operation expected.');
                    return 1;
                }

                return 0;
            } catch (e) {
                console.error('[OS] Storage rmdir error:', e);
                const msg = e.message || 'Failed to remove directory';
                const len = lengthBytesUTF8(msg) + 1;
                const ptr = _malloc(len);
                stringToUTF8(msg, ptr, len);
                Module._last_os_error = ptr;
                return 1;
            }
        }
    }
    return -1;
});

// Stat file/directory via storage peripheral
// Returns: JSON string with stat info or null
EM_JS(char*, storage_stat, (const char *path), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.stat === 'function') {
            try {
                const pathStr = UTF8ToString(path);
                const resultPromiseOrValue = storage.stat(pathStr);

                // Check if result is a Promise
                if (resultPromiseOrValue && typeof resultPromiseOrValue.then === 'function') {
                    console.warn('[OS] Storage stat returned Promise, but sync operation expected.');
                    return null;
                }

                // Handle synchronous result
                const stat = resultPromiseOrValue;
                if (stat) {
                    const json = JSON.stringify(stat);
                    const len = lengthBytesUTF8(json) + 1;
                    const ptr = _malloc(len);
                    stringToUTF8(json, ptr, len);
                    return ptr;
                }
            } catch (e) {
                console.error('[OS] Storage stat error:', e);
                const msg = e.message || 'Failed to stat file';
                const len = lengthBytesUTF8(msg) + 1;
                const ptr = _malloc(len);
                stringToUTF8(msg, ptr, len);
                Module._last_os_error = ptr;
                return null;
            }
        }
    }
    return null;
});

// Statvfs via storage peripheral
// Returns: JSON string with statvfs info or null
EM_JS(char*, storage_statvfs, (const char *path), {
    if (Module.hasPeripheral && Module.hasPeripheral('storage')) {
        const storage = Module.getPeripheral('storage');
        if (storage && typeof storage.statvfs === 'function') {
            try {
                const pathStr = UTF8ToString(path);
                const result = storage.statvfs(pathStr);
                if (result) {
                    const json = JSON.stringify(result);
                    const len = lengthBytesUTF8(json) + 1;
                    const ptr = _malloc(len);
                    stringToUTF8(json, ptr, len);
                    return ptr;
                }
            } catch (e) {
                console.error('[OS] Storage statvfs error:', e);
            }
        }
    }
    return null;
});

// Get last error message
EM_JS(char*, get_last_os_error, (), {
    if (Module._last_os_error) {
        const ptr = Module._last_os_error;
        Module._last_os_error = null;
        return ptr;
    }
    return null;
});

// ========== Common HAL Implementation ==========

void common_hal_os_chdir(const char *path) {
    if (!has_storage_peripheral()) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral not available"));
    }

    int result = storage_chdir(path);
    if (result == -1) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral does not support chdir"));
    } else if (result == 1) {
        char *error_msg = get_last_os_error();
        if (error_msg) {
            mp_obj_t exc = mp_obj_new_exception_msg(&mp_type_OSError, error_msg);
            free(error_msg);
            nlr_raise(exc);
        }
        mp_raise_OSError(MP_ENOENT);
    }
}

mp_obj_t common_hal_os_getcwd(void) {
    if (!has_storage_peripheral()) {
        return mp_obj_new_str("/", 1);
    }

    char *cwd = storage_getcwd();
    if (cwd) {
        mp_obj_t result = mp_obj_new_str(cwd, strlen(cwd));
        free(cwd);
        return result;
    }

    // Default to root directory
    return mp_obj_new_str("/", 1);
}

mp_obj_t common_hal_os_listdir(const char *path) {
    if (!has_storage_peripheral()) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral not available"));
    }

    char *json = storage_listdir(path);
    if (!json) {
        char *error_msg = get_last_os_error();
        if (error_msg) {
            mp_obj_t exc = mp_obj_new_exception_msg(&mp_type_OSError, error_msg);
            free(error_msg);
            nlr_raise(exc);
        }
        mp_raise_OSError(MP_ENOENT);
    }

    // Parse JSON array and create Python list
    mp_obj_t list = mp_obj_new_list(0, NULL);

    // Simple JSON parsing for array of strings
    const char *p = json;
    while (*p && *p != '[') p++;
    if (*p == '[') p++;

    while (*p) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

        if (*p == '"') {
            // Found start of string
            p++;
            const char *start = p;
            while (*p && *p != '"') {
                if (*p == '\\') p++; // Skip escaped character
                p++;
            }

            if (*p == '"') {
                size_t len = p - start;
                char *name = malloc(len + 1);
                strncpy(name, start, len);
                name[len] = '\0';

                mp_obj_t name_obj = mp_obj_new_str(name, len);
                mp_obj_list_append(list, name_obj);
                free(name);

                p++;
            }
        } else if (*p == ']') {
            break;
        } else if (*p == ',') {
            p++;
        } else if (*p) {
            p++;
        }
    }

    free(json);
    return list;
}

void common_hal_os_mkdir(const char *path) {
    if (!has_storage_peripheral()) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral not available"));
    }

    int result = storage_mkdir(path);
    if (result == -1) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral does not support mkdir"));
    } else if (result == 1) {
        char *error_msg = get_last_os_error();
        if (error_msg) {
            mp_obj_t exc = mp_obj_new_exception_msg(&mp_type_OSError, error_msg);
            free(error_msg);
            nlr_raise(exc);
        }
        mp_raise_OSError(MP_EIO);
    }
}

void common_hal_os_remove(const char *path) {
    if (!has_storage_peripheral()) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral not available"));
    }

    int result = storage_remove(path);
    if (result == -1) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral does not support remove"));
    } else if (result == 1) {
        char *error_msg = get_last_os_error();
        if (error_msg) {
            mp_obj_t exc = mp_obj_new_exception_msg(&mp_type_OSError, error_msg);
            free(error_msg);
            nlr_raise(exc);
        }
        mp_raise_OSError(MP_ENOENT);
    }
}

void common_hal_os_rename(const char *old_path, const char *new_path) {
    // Rename is not directly supported by storage peripheral interface
    // This would need to be implemented as read+write+delete
    mp_raise_NotImplementedError(MP_ERROR_TEXT("rename not yet implemented for WASM"));
}

void common_hal_os_rmdir(const char *path) {
    if (!has_storage_peripheral()) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral not available"));
    }

    int result = storage_rmdir(path);
    if (result == -1) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral does not support rmdir"));
    } else if (result == 1) {
        char *error_msg = get_last_os_error();
        if (error_msg) {
            mp_obj_t exc = mp_obj_new_exception_msg(&mp_type_OSError, error_msg);
            free(error_msg);
            nlr_raise(exc);
        }
        mp_raise_OSError(MP_ENOTEMPTY);
    }
}

mp_obj_t common_hal_os_stat(const char *path) {
    if (!has_storage_peripheral()) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral not available"));
    }

    char *json = storage_stat(path);
    if (!json) {
        char *error_msg = get_last_os_error();
        if (error_msg) {
            mp_obj_t exc = mp_obj_new_exception_msg(&mp_type_OSError, error_msg);
            free(error_msg);
            nlr_raise(exc);
        }
        mp_raise_OSError(MP_ENOENT);
    }

    // Parse JSON and create stat tuple
    // Expected fields: size, mode, isDirectory, mtime
    mp_int_t size = 0;
    mp_int_t mode = 0;
    mp_int_t mtime = 0;
    bool is_dir = false;

    // Simple JSON parsing
    const char *p = json;
    while (*p) {
        if (strncmp(p, "\"size\":", 7) == 0) {
            p += 7;
            while (*p == ' ') p++;
            size = atoi(p);
        } else if (strncmp(p, "\"mode\":", 7) == 0) {
            p += 7;
            while (*p == ' ') p++;
            mode = atoi(p);
        } else if (strncmp(p, "\"mtime\":", 8) == 0) {
            p += 8;
            while (*p == ' ') p++;
            mtime = atoi(p);
        } else if (strncmp(p, "\"isDirectory\":", 14) == 0) {
            p += 14;
            while (*p == ' ') p++;
            is_dir = (strncmp(p, "true", 4) == 0);
        }
        p++;
    }

    free(json);

    // Create stat tuple: (mode, ino, dev, nlink, uid, gid, size, atime, mtime, ctime)
    mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    tuple->items[0] = MP_OBJ_NEW_SMALL_INT(mode);     // st_mode
    tuple->items[1] = MP_OBJ_NEW_SMALL_INT(0);         // st_ino
    tuple->items[2] = MP_OBJ_NEW_SMALL_INT(0);         // st_dev
    tuple->items[3] = MP_OBJ_NEW_SMALL_INT(0);         // st_nlink
    tuple->items[4] = MP_OBJ_NEW_SMALL_INT(0);         // st_uid
    tuple->items[5] = MP_OBJ_NEW_SMALL_INT(0);         // st_gid
    tuple->items[6] = MP_OBJ_NEW_SMALL_INT(size);      // st_size
    tuple->items[7] = MP_OBJ_NEW_SMALL_INT(mtime);     // st_atime
    tuple->items[8] = MP_OBJ_NEW_SMALL_INT(mtime);     // st_mtime
    tuple->items[9] = MP_OBJ_NEW_SMALL_INT(mtime);     // st_ctime

    return MP_OBJ_FROM_PTR(tuple);
}

mp_obj_t common_hal_os_statvfs(const char *path) {
    if (!has_storage_peripheral()) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("Storage peripheral not available"));
    }

    char *json = storage_statvfs(path);
    if (!json) {
        // Return default values if not supported
        mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
        tuple->items[0] = MP_OBJ_NEW_SMALL_INT(4096);  // f_bsize
        tuple->items[1] = MP_OBJ_NEW_SMALL_INT(4096);  // f_frsize
        tuple->items[2] = MP_OBJ_NEW_SMALL_INT(0);     // f_blocks
        tuple->items[3] = MP_OBJ_NEW_SMALL_INT(0);     // f_bfree
        tuple->items[4] = MP_OBJ_NEW_SMALL_INT(0);     // f_bavail
        tuple->items[5] = MP_OBJ_NEW_SMALL_INT(0);     // f_files
        tuple->items[6] = MP_OBJ_NEW_SMALL_INT(0);     // f_ffree
        tuple->items[7] = MP_OBJ_NEW_SMALL_INT(0);     // f_favail
        tuple->items[8] = MP_OBJ_NEW_SMALL_INT(0);     // f_flag
        tuple->items[9] = MP_OBJ_NEW_SMALL_INT(255);   // f_namemax
        return MP_OBJ_FROM_PTR(tuple);
    }

    // Parse JSON and create statvfs tuple
    mp_int_t blockSize = 4096;
    mp_int_t totalBlocks = 0;
    mp_int_t freeBlocks = 0;
    mp_int_t availBlocks = 0;

    const char *p = json;
    while (*p) {
        if (strncmp(p, "\"blockSize\":", 12) == 0) {
            p += 12;
            while (*p == ' ') p++;
            blockSize = atoi(p);
        } else if (strncmp(p, "\"totalBlocks\":", 14) == 0) {
            p += 14;
            while (*p == ' ') p++;
            totalBlocks = atoi(p);
        } else if (strncmp(p, "\"freeBlocks\":", 13) == 0) {
            p += 13;
            while (*p == ' ') p++;
            freeBlocks = atoi(p);
        } else if (strncmp(p, "\"availBlocks\":", 14) == 0) {
            p += 14;
            while (*p == ' ') p++;
            availBlocks = atoi(p);
        }
        p++;
    }

    free(json);

    // Create statvfs tuple
    mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    tuple->items[0] = MP_OBJ_NEW_SMALL_INT(blockSize);    // f_bsize
    tuple->items[1] = MP_OBJ_NEW_SMALL_INT(blockSize);    // f_frsize
    tuple->items[2] = MP_OBJ_NEW_SMALL_INT(totalBlocks);  // f_blocks
    tuple->items[3] = MP_OBJ_NEW_SMALL_INT(freeBlocks);   // f_bfree
    tuple->items[4] = MP_OBJ_NEW_SMALL_INT(availBlocks);  // f_bavail
    tuple->items[5] = MP_OBJ_NEW_SMALL_INT(0);            // f_files
    tuple->items[6] = MP_OBJ_NEW_SMALL_INT(0);            // f_ffree
    tuple->items[7] = MP_OBJ_NEW_SMALL_INT(0);            // f_favail
    tuple->items[8] = MP_OBJ_NEW_SMALL_INT(0);            // f_flag
    tuple->items[9] = MP_OBJ_NEW_SMALL_INT(255);          // f_namemax

    return MP_OBJ_FROM_PTR(tuple);
}

void common_hal_os_utime(const char *path, mp_obj_t times) {
    // utime is optional and not commonly needed in browser environment
    // Storage peripheral can optionally implement it
    mp_raise_NotImplementedError(MP_ERROR_TEXT("utime not implemented for WASM"));
}

bool common_hal_os_urandom(uint8_t *buffer, mp_uint_t length) {
    // Use JavaScript's crypto.getRandomValues for secure random numbers
    EM_ASM({
        if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
            const buffer = new Uint8Array(Module.HEAPU8.buffer, $0, $1);
            crypto.getRandomValues(buffer);
        } else {
            // Fallback to Math.random (not cryptographically secure)
            const buffer = new Uint8Array(Module.HEAPU8.buffer, $0, $1);
            for (let i = 0; i < $1; i++) {
                buffer[i] = Math.floor(Math.random() * 256);
            }
        }
    }, buffer, length);

    return true;
}

#if CIRCUITPY_OS_GETENV
mp_obj_t common_hal_os_getenv(const char *key, mp_obj_t default_val) {
    // Environment variables not currently supported in WASM
    // Could be implemented via storage peripheral or browser localStorage
    return default_val;
}
#endif
