// Dynamic module loading system for CircuitPython WebAssembly
// Implements Phase 2 of the architecture: runtime .py file importing

#include <string.h>
#include <stdio.h>
#include <emscripten.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/compile.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/mperrno.h"
#include "py/mphal.h"

// Module cache for loaded modules
static mp_obj_dict_t *module_cache = NULL;

// Register the module cache with garbage collector
MP_REGISTER_ROOT_POINTER(mp_obj_dict_t *module_cache);

// Initialize the dynamic module system
void dynamic_modules_init(void) {
    if (module_cache == NULL) {
        module_cache = mp_obj_new_dict(8);
        // Module cache is now registered with GC via MP_REGISTER_ROOT_POINTER
    }
}

// JavaScript interface for fetching module source
// Called from JavaScript to provide module source code
EM_JS(char*, fetch_module_source_js, (const char* module_name), {
    // This will be implemented by the JavaScript runtime
    // Browser: fetch from URL, Node.js: read from filesystem
    if (typeof Module.fetchModuleSource === 'function') {
        try {
            const source = Module.fetchModuleSource(UTF8ToString(module_name));
            if (source) {
                const len = lengthBytesUTF8(source) + 1;
                const ptr = _malloc(len);
                stringToUTF8(source, ptr, len);
                return ptr;
            }
        } catch (e) {
            console.error('Error fetching module:', e);
        }
    }
    return 0; // NULL
});

// Load and compile a Python module from source
mp_obj_t load_module_from_source(const char* module_name, const char* source) {
    if (!source || strlen(source) == 0) {
        mp_raise_msg(&mp_type_ImportError, MP_ERROR_TEXT("Module source is empty"));
    }
    
    // Create lexer from string
    mp_lexer_t *lex = mp_lexer_new_from_str_len(qstr_from_str(module_name), source, strlen(source), 0);
    if (lex == NULL) {
        mp_raise_msg(&mp_type_ImportError, MP_ERROR_TEXT("Failed to create lexer for module"));
    }
    
    // Parse the module
    mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
    
    // Check for parse errors
    if (parse_tree.root == MP_PARSE_NODE_NULL) {
        // Clean up lexer on parse error
        mp_lexer_free(lex);
        mp_raise_msg(&mp_type_SyntaxError, MP_ERROR_TEXT("Failed to parse module"));
    }
    
    // Compile the module (this also frees the parse tree)
    mp_obj_t module_fun = mp_compile(&parse_tree, qstr_from_str(module_name), false);
    
    // Lexer is freed by mp_compile, don't need to free it manually
    
    // Create module object
    mp_obj_module_t *module = mp_obj_new_module(qstr_from_str(module_name));
    
    // Set module globals
    mp_obj_dict_store(MP_OBJ_FROM_PTR(module->globals), MP_OBJ_NEW_QSTR(MP_QSTR___name__), 
                      mp_obj_new_str(module_name, strlen(module_name)));
    
    // Execute module in its namespace
    mp_obj_t old_globals = mp_globals_get();
    mp_globals_set(module->globals);
    
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        // Restore globals and re-raise exception
        // Note: module and module_fun are GC objects, no manual cleanup needed
        mp_globals_set(old_globals);
        nlr_jump(nlr.ret_val);
    }
    
    mp_globals_set(old_globals);
    
    return MP_OBJ_FROM_PTR(module);
}

// Dynamic import function callable from Python
mp_obj_t mp_dynamic_import(mp_obj_t module_name_obj) {
    // Convert module name to C string
    const char *module_name = mp_obj_str_get_str(module_name_obj);
    
    // Check cache first
    if (module_cache != NULL) {
        mp_obj_t cached = mp_obj_dict_get(MP_OBJ_FROM_PTR(module_cache), module_name_obj);
        if (cached != MP_OBJ_NULL) {
            return cached;
        }
    }
    
    // Fetch source from JavaScript
    char *source = fetch_module_source_js(module_name);
    if (source == NULL) {
        mp_raise_msg_varg(&mp_type_ImportError, MP_ERROR_TEXT("Cannot find module '%s'"), module_name);
    }
    
    // Load and compile module
    mp_obj_t module = load_module_from_source(module_name, source);
    
    // Cache the module
    if (module_cache != NULL) {
        mp_obj_dict_store(MP_OBJ_FROM_PTR(module_cache), module_name_obj, module);
    }
    
    // Free the source string allocated by JavaScript
    free(source);
    
    return module;
}

// Clear module cache (for hot-reload support)
void dynamic_modules_clear_cache(void) {
    if (module_cache != NULL) {
        // Clear all entries in the dictionary
        module_cache->map.used = 0;
    }
}

// Deinitialize the dynamic module system
void dynamic_modules_deinit(void) {
    // Clear the cache
    dynamic_modules_clear_cache();
    // Set cache to NULL - GC will clean up the memory
    module_cache = NULL;
}

// Export dynamic import function for Python access
static MP_DEFINE_CONST_FUN_OBJ_1(mp_dynamic_import_obj, mp_dynamic_import);

// Register the dynamic import system
void dynamic_modules_register(void) {
    dynamic_modules_init();
    
    // Add dynamic_import to the global namespace
    mp_store_global(MP_QSTR_dynamic_import, MP_OBJ_FROM_PTR(&mp_dynamic_import_obj));
    
    // printf("Dynamic module system initialized\n");
}