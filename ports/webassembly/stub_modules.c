/*
 * WebAssembly stubs for modules not included in CircuitPython's extmod.mk
 * but referenced by core py/ code
 */

#include "py/obj.h"
#include "py/runtime.h"
#include "py/lexer.h"
#include "py/mperrno.h"

// Simple stubs for missing symbols - these will be replaced by extmod-wasm.mk later

#if MICROPY_PY_MICROPYTHON_RINGIO
// Ring I/O buffer type - minimal stub implementation
const mp_obj_type_t mp_type_ringio = {
    { &mp_type_type },
    .name = MP_QSTR_RingIO,
};
#endif

#if MICROPY_PY_UCTYPES  
// uctypes module stub - module object for parser/import system
const mp_obj_module_t mp_module_uctypes = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_const_empty_dict_obj,
};
#endif

// Simple stub for file lexer - just raise an error
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    mp_raise_OSError(MP_ENOENT);
}