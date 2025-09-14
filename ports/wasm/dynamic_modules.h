// Dynamic module loading system for CircuitPython WebAssembly
// Phase 2 implementation: runtime .py file importing

#ifndef DYNAMIC_MODULES_H
#define DYNAMIC_MODULES_H

#include "py/obj.h"

// Initialize the dynamic module system
void dynamic_modules_init(void);

// Register dynamic import functions with MicroPython
void dynamic_modules_register(void);

// Load module from source code
mp_obj_t load_module_from_source(const char* module_name, const char* source);

// Dynamic import function callable from Python
mp_obj_t mp_dynamic_import(mp_obj_t module_name_obj);

// Clear module cache for hot-reload
void dynamic_modules_clear_cache(void);

// Deinitialize the dynamic module system
void dynamic_modules_deinit(void);

#endif // DYNAMIC_MODULES_H