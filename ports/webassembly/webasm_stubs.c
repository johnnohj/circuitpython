/*
 * WebAssembly stubs
 * Provides stubs for modules and functions that CircuitPython removed
 * but are still referenced by core py/ code
 */

#include "py/obj.h"
#include "py/runtime.h"
#include "py/lexer.h"
#include "py/mperrno.h"

// uctypes module stub - CircuitPython removed this entirely
const mp_obj_module_t mp_module_uctypes = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_const_empty_dict_obj,
};

// RingIO type stub - simplified implementation
const mp_obj_type_t mp_type_ringio = {
    { &mp_type_type },
    .name = MP_QSTR_RingIO,
};

// Simple REPL stubs for WebAssembly (when not using event-driven REPL)
// COMMENTED OUT: Use real REPL implementations instead of stubs
/*
void pyexec_event_repl_init(void) {
    // Initialize basic REPL state - no-op for WebAssembly
}

int pyexec_event_repl_process_char(int c) {
    // Process a single character in REPL - basic implementation
    // For WebAssembly, we can just return that input is complete
    return 0; // No special handling needed
}
*/

// Lexer function for VFS - simple stub
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    // For WebAssembly without full VFS, files aren't supported
    mp_raise_OSError(MP_ENOENT);
}