// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// OS module implementation for WASM port using storage peripheral

#ifndef MICROPY_INCLUDED_WASM_COMMON_HAL_OS___INIT___H
#define MICROPY_INCLUDED_WASM_COMMON_HAL_OS___INIT___H

#include "py/obj.h"

// Filesystem operations using storage peripheral
void common_hal_os_chdir(const char *path);
mp_obj_t common_hal_os_getcwd(void);
mp_obj_t common_hal_os_listdir(const char *path);
void common_hal_os_mkdir(const char *path);
void common_hal_os_remove(const char *path);
void common_hal_os_rename(const char *old_path, const char *new_path);
void common_hal_os_rmdir(const char *path);
mp_obj_t common_hal_os_stat(const char *path);
mp_obj_t common_hal_os_statvfs(const char *path);
void common_hal_os_utime(const char *path, mp_obj_t times);

// Random number generation
bool common_hal_os_urandom(uint8_t *buffer, mp_uint_t length);

// Environment variables (optional)
#if CIRCUITPY_OS_GETENV
mp_obj_t common_hal_os_getenv(const char *key, mp_obj_t default_val);
#endif

#endif // MICROPY_INCLUDED_WASM_COMMON_HAL_OS___INIT___H
