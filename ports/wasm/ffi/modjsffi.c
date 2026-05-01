// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/modjsffi.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT

/*
 * modjsffi.c — jsffi module: JavaScript FFI for CircuitPython WASM.
 *
 * Provides:
 *   jsffi.JsProxy       — type for wrapped JS objects
 *   jsffi.JsException   — exception from JS code
 *   jsffi.global_this    — the JS globalThis object
 *   jsffi.create_proxy() — wrap Python object as JS PyProxy
 *   jsffi.to_js()        — convert Python to native JS type
 *   jsffi.mem_info()     — proxy table diagnostics
 *
 * Adapted from MicroPython's ports/webassembly/modjsffi.c.
 */

#include "py/gc.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "ffi/proxy_c.h"
#include "ffi/jsffi_imports.h"

#if MICROPY_PY_JSFFI

/* ------------------------------------------------------------------ */
/* jsffi.create_proxy(obj) — wrap Python object for JS callback use    */
/* ------------------------------------------------------------------ */

static mp_obj_t mp_jsffi_create_proxy(mp_obj_t arg) {
    /* Convert Python object to C→JS PVN (kind=CALLABLE/OBJECT, c_ref).
     * JS creates a PyProxy wrapper and writes back a JS→C PVN
     * (kind=JS_OBJECT, js_ref) referencing that PyProxy. */
    uint32_t out[PVN];
    proxy_convert_mp_to_js_obj_cside(arg, out);
    jsffi_create_pyproxy(out);
    return proxy_convert_js_to_mp_obj_cside(out);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_jsffi_create_proxy_obj,
                                 mp_jsffi_create_proxy);

/* ------------------------------------------------------------------ */
/* jsffi.to_js(obj) — convert Python object to native JS              */
/* ------------------------------------------------------------------ */

static mp_obj_t mp_jsffi_to_js(mp_obj_t arg) {
    /* Convert Python object to C→JS PVN, then JS unwraps any PyProxy
     * to its underlying JS value, writes back a JS→C PVN. */
    uint32_t out[PVN];
    proxy_convert_mp_to_js_obj_cside(arg, out);
    jsffi_to_js(out);
    return proxy_convert_js_to_mp_obj_cside(out);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_jsffi_to_js_obj, mp_jsffi_to_js);

/* ------------------------------------------------------------------ */
/* jsffi.mem_info() — proxy table diagnostics                          */
/* ------------------------------------------------------------------ */

static mp_obj_t mp_jsffi_mem_info(void) {
    mp_obj_list_t *l = (mp_obj_list_t *)MP_OBJ_TO_PTR(MP_STATE_PORT(proxy_c_ref));
    mp_int_t used = 0;
    for (size_t i = 0; i < l->len; ++i) {
        if (l->items[i] != MP_OBJ_NULL) {
            ++used;
        }
    }
    gc_info_t info;
    gc_info(&info);
    mp_obj_t elems[] = {
        MP_OBJ_NEW_SMALL_INT(info.total),
        MP_OBJ_NEW_SMALL_INT(info.used),
        MP_OBJ_NEW_SMALL_INT(info.free),
        MP_OBJ_NEW_SMALL_INT(l->len),
        MP_OBJ_NEW_SMALL_INT(used),
    };
    return mp_obj_new_tuple(MP_ARRAY_SIZE(elems), elems);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_jsffi_mem_info_obj, mp_jsffi_mem_info);

/* ------------------------------------------------------------------ */
/* Module __attr__ — dynamic attributes like global_this               */
/* ------------------------------------------------------------------ */

void mp_module_jsffi_attr(mp_obj_t self_in, qstr attr,
                          mp_obj_t *dest) {
    if (dest[0] == MP_OBJ_NULL) {
        /* Load attribute. */
        if (attr == MP_QSTR_global_this) {
            dest[0] = MP_STATE_PORT(jsproxy_global_this);
            return;
        }
        /* For unknown attributes, try globalThis as fallback —
         * jsffi.console is shorthand for jsffi.global_this.console. */
        mp_obj_jsproxy_global_this_attr(attr, dest);
    }
}

/* ------------------------------------------------------------------ */
/* Module definition                                                   */
/* ------------------------------------------------------------------ */

static const mp_rom_map_elem_t mp_module_jsffi_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_jsffi) },

    { MP_ROM_QSTR(MP_QSTR_JsProxy), MP_ROM_PTR(&mp_type_jsproxy) },
    { MP_ROM_QSTR(MP_QSTR_JsException), MP_ROM_PTR(&mp_type_JsException) },
    { MP_ROM_QSTR(MP_QSTR_create_proxy), MP_ROM_PTR(&mp_jsffi_create_proxy_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_js), MP_ROM_PTR(&mp_jsffi_to_js_obj) },
    { MP_ROM_QSTR(MP_QSTR_mem_info), MP_ROM_PTR(&mp_jsffi_mem_info_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_jsffi_globals,
                            mp_module_jsffi_globals_table);

const mp_obj_module_t mp_module_jsffi = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_jsffi_globals,
};

MP_REGISTER_MODULE(MP_QSTR_jsffi, mp_module_jsffi);
MP_REGISTER_MODULE_DELEGATION(mp_module_jsffi, mp_module_jsffi_attr);

#endif /* MICROPY_PY_JSFFI */
