// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/proxy_c.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT

/*
 * proxy_c.c — C-side proxy reference management and PVN marshaling.
 *
 * Adapted from MicroPython's ports/webassembly/proxy_c.c.
 * Changes for WASM-dist port:
 *   - No Emscripten: EM_JS replaced with jsffi WASM imports
 *   - JS→C callback functions exported via __attribute__((export_name))
 *   - Async/Promise bridging deferred to Phase 4
 */

#include <stdlib.h>
#include <string.h>

#include "py/builtin.h"
#include "py/runtime.h"
#include "py/gc.h"
#include "ffi/proxy_c.h"
#include "ffi/jsffi_imports.h"

#if MICROPY_PY_JSFFI

/* ------------------------------------------------------------------ */
/* Re-entrancy depth tracking                                          */
/* ------------------------------------------------------------------ */

static int external_call_depth = 0;

void external_call_depth_inc(void) {
    ++external_call_depth;
}

void external_call_depth_dec(void) {
    --external_call_depth;
}

/* ------------------------------------------------------------------ */
/* mp_const_undefined — represents JS undefined                        */
/* ------------------------------------------------------------------ */

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_undefined,
    MP_QSTR_undefined,
    MP_TYPE_FLAG_NONE
    );

static const mp_obj_base_t mp_const_undefined_obj = {&mp_type_undefined};
#define mp_const_undefined (MP_OBJ_FROM_PTR(&mp_const_undefined_obj))

/* ------------------------------------------------------------------ */
/* JsException type                                                    */
/* ------------------------------------------------------------------ */

MP_DEFINE_EXCEPTION(JsException, Exception)

/* ------------------------------------------------------------------ */
/* C-side reference table (proxy_c_ref)                                */
/*                                                                     */
/* Python objects exported to JS.  Indexed by c_ref.                   */
/* Stored as a MicroPython list in MP_STATE_PORT(proxy_c_ref).         */
/* proxy_c_dict maps object pointer → c_ref for deduplication.         */
/* ------------------------------------------------------------------ */

#define PROXY_C_REF_NUM_STATIC (1)

static size_t proxy_c_ref_next;

void proxy_c_init(void) {
    MP_STATE_PORT(proxy_c_ref) = mp_obj_new_list(0, NULL);
    MP_STATE_PORT(proxy_c_dict) = mp_obj_new_dict(0);
    mp_obj_list_append(MP_STATE_PORT(proxy_c_ref), MP_OBJ_NULL);
    proxy_c_ref_next = PROXY_C_REF_NUM_STATIC;

    extern void mp_obj_jsproxy_init(void);
    mp_obj_jsproxy_init();
}

MP_REGISTER_ROOT_POINTER(mp_obj_t proxy_c_ref);
MP_REGISTER_ROOT_POINTER(mp_obj_t proxy_c_dict);

size_t proxy_c_add_obj(mp_obj_t obj) {
    size_t id = 0;
    mp_obj_list_t *l = (mp_obj_list_t *)MP_OBJ_TO_PTR(MP_STATE_PORT(proxy_c_ref));
    while (proxy_c_ref_next < l->len) {
        if (l->items[proxy_c_ref_next] == MP_OBJ_NULL) {
            id = proxy_c_ref_next;
            ++proxy_c_ref_next;
            l->items[id] = obj;
            break;
        }
        ++proxy_c_ref_next;
    }

    if (id == 0) {
        id = l->len;
        mp_obj_list_append(MP_STATE_PORT(proxy_c_ref), obj);
        proxy_c_ref_next = l->len;
    }

    mp_obj_t obj_key = mp_obj_new_int_from_uint((uintptr_t)obj);
    mp_map_elem_t *elem = mp_map_lookup(
        mp_obj_dict_get_map(MP_STATE_PORT(proxy_c_dict)),
        obj_key, MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
    elem->value = mp_obj_new_int_from_uint(id);

    return id;
}

static inline int proxy_c_check_existing(mp_obj_t obj) {
    mp_obj_t obj_key = mp_obj_new_int_from_uint((uintptr_t)obj);
    mp_map_elem_t *elem = mp_map_lookup(
        mp_obj_dict_get_map(MP_STATE_PORT(proxy_c_dict)),
        obj_key, MP_MAP_LOOKUP);
    if (elem == NULL) {
        return -1;
    }
    uint32_t c_ref = mp_obj_int_get_truncated(elem->value);
    return jsffi_check_existing(c_ref);
}

mp_obj_t proxy_c_get_obj(uint32_t c_ref) {
    return ((mp_obj_list_t *)MP_OBJ_TO_PTR(MP_STATE_PORT(proxy_c_ref)))->items[c_ref];
}

void proxy_c_free_obj(uint32_t c_ref) {
    if (c_ref >= PROXY_C_REF_NUM_STATIC) {
        mp_obj_t obj_key = mp_obj_new_int_from_uint((uintptr_t)proxy_c_get_obj(c_ref));
        mp_map_elem_t *elem = mp_map_lookup(
            mp_obj_dict_get_map(MP_STATE_PORT(proxy_c_dict)),
            obj_key, MP_MAP_LOOKUP);
        if (elem != NULL && mp_obj_int_get_truncated(elem->value) == c_ref) {
            mp_map_lookup(mp_obj_dict_get_map(MP_STATE_PORT(proxy_c_dict)),
                          obj_key, MP_MAP_LOOKUP_REMOVE_IF_FOUND);
        }
        ((mp_obj_list_t *)MP_OBJ_TO_PTR(MP_STATE_PORT(proxy_c_ref)))->items[c_ref] = MP_OBJ_NULL;
        proxy_c_ref_next = MIN(proxy_c_ref_next, c_ref);
    }
}

/* ------------------------------------------------------------------ */
/* PVN conversion: JS → mp_obj_t                                       */
/* ------------------------------------------------------------------ */

mp_obj_t proxy_convert_js_to_mp_obj_cside(uint32_t *value) {
    if (value[0] == PROXY_KIND_JS_UNDEFINED) {
        return mp_const_undefined;
    } else if (value[0] == PROXY_KIND_JS_NULL) {
        return mp_const_none;
    } else if (value[0] == PROXY_KIND_JS_BOOLEAN) {
        return mp_obj_new_bool(value[1]);
    } else if (value[0] == PROXY_KIND_JS_INTEGER) {
        return mp_obj_new_int(value[1]);
    } else if (value[0] == PROXY_KIND_JS_DOUBLE) {
        return mp_obj_new_float_from_d(*(double *)&value[1]);
    } else if (value[0] == PROXY_KIND_JS_STRING) {
        mp_obj_t s = mp_obj_new_str((void *)value[2], value[1]);
        free((void *)value[2]);
        return s;
    } else if (value[0] == PROXY_KIND_JS_PYPROXY) {
        return proxy_c_get_obj(value[1]);
    } else if (value[0] == PROXY_KIND_JS_OBJECT_EXISTING) {
        return mp_obj_get_jsproxy(value[1]);
    } else {
        // PROXY_KIND_JS_OBJECT
        return mp_obj_new_jsproxy(value[1]);
    }
}

/* ------------------------------------------------------------------ */
/* PVN conversion: mp_obj_t → JS                                       */
/* ------------------------------------------------------------------ */

void proxy_convert_mp_to_js_obj_cside(mp_obj_t obj, uint32_t *out) {
    uint32_t kind;
    int js_ref;
    if (obj == MP_OBJ_NULL) {
        kind = PROXY_KIND_MP_NULL;
    } else if (obj == mp_const_none) {
        kind = PROXY_KIND_MP_NONE;
    } else if (mp_obj_is_bool(obj)) {
        kind = PROXY_KIND_MP_BOOL;
        out[1] = mp_obj_is_true(obj);
    } else if (mp_obj_is_int(obj)) {
        kind = PROXY_KIND_MP_INT;
        out[1] = mp_obj_get_int_truncated(obj);
    } else if (mp_obj_is_float(obj)) {
        kind = PROXY_KIND_MP_FLOAT;
        *(double *)&out[1] = (double)mp_obj_get_float(obj);
    } else if (mp_obj_is_str(obj)) {
        kind = PROXY_KIND_MP_STR;
        size_t len;
        const char *str = mp_obj_str_get_data(obj, &len);
        out[1] = len;
        out[2] = (uintptr_t)str;
    } else if (obj == mp_const_undefined) {
        kind = PROXY_KIND_MP_JSPROXY;
        out[1] = JSPROXY_REF_UNDEFINED;
    } else if (mp_obj_is_jsproxy(obj)) {
        kind = PROXY_KIND_MP_JSPROXY;
        out[1] = mp_obj_jsproxy_get_ref(obj);
    } else if ((js_ref = proxy_c_check_existing(obj)) >= 0) {
        kind = PROXY_KIND_MP_EXISTING;
        out[1] = js_ref;
    } else if (mp_obj_get_type(obj) == &mp_type_JsException) {
        mp_obj_exception_t *exc = MP_OBJ_TO_PTR(obj);
        if (exc->args->len > 0 && mp_obj_is_jsproxy(exc->args->items[0])) {
            kind = PROXY_KIND_MP_JSPROXY;
            out[1] = mp_obj_jsproxy_get_ref(exc->args->items[0]);
        } else {
            kind = PROXY_KIND_MP_OBJECT;
            out[1] = proxy_c_add_obj(obj);
        }
    } else {
        if (mp_obj_is_callable(obj)) {
            kind = PROXY_KIND_MP_CALLABLE;
        } else if (mp_obj_is_type(obj, &mp_type_gen_instance)) {
            kind = PROXY_KIND_MP_GENERATOR;
        } else {
            kind = PROXY_KIND_MP_OBJECT;
        }
        out[1] = proxy_c_add_obj(obj);
    }
    out[0] = kind;
}

/* ------------------------------------------------------------------ */
/* Exception serialization                                             */
/* ------------------------------------------------------------------ */

void proxy_convert_mp_to_js_exc_cside(void *exc, uint32_t *out) {
    out[0] = PROXY_KIND_MP_EXCEPTION;
    vstr_t vstr;
    mp_print_t print;
    vstr_init_print(&vstr, 64, &print);
    vstr_add_str(&vstr, qstr_str(mp_obj_get_type(MP_OBJ_FROM_PTR(exc))->name));
    vstr_add_char(&vstr, '\x04');
    mp_obj_print_exception(&print, MP_OBJ_FROM_PTR(exc));
    char *s = malloc(vstr_len(&vstr) + 1);
    memcpy(s, vstr_str(&vstr), vstr_len(&vstr));
    out[1] = vstr_len(&vstr);
    out[2] = (uintptr_t)s;
    vstr_clear(&vstr);
}

/* ------------------------------------------------------------------ */
/* JsException creation from JS Error object                           */
/* ------------------------------------------------------------------ */

mp_obj_t mp_obj_jsproxy_make_js_exception(mp_obj_t error) {
    uint32_t out_name[PVN];
    uint32_t out_message[PVN];
    jsffi_get_error_info(mp_obj_jsproxy_get_ref(error), out_name, out_message);
    mp_obj_t args[3] = {
        error,
        proxy_convert_js_to_mp_obj_cside(out_name),
        proxy_convert_js_to_mp_obj_cside(out_message),
    };
    return mp_obj_new_exception_args(&mp_type_JsException, MP_ARRAY_SIZE(args), args);
}

/* ------------------------------------------------------------------ */
/* JS→C callback entry points (exported to WASM)                       */
/*                                                                     */
/* These are called by JS (via PyProxy) to invoke Python objects.       */
/* ------------------------------------------------------------------ */

__attribute__((export_name("proxy_c_to_js_call")))
void proxy_c_to_js_call(uint32_t c_ref, uint32_t n_args,
                        uint32_t *args_value, uint32_t *out) {
    external_call_depth_inc();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args[n_args];
        for (size_t i = 0; i < n_args; ++i) {
            args[i] = proxy_convert_js_to_mp_obj_cside(args_value + i * PVN);
        }
        mp_obj_t obj = proxy_c_get_obj(c_ref);
        mp_obj_t member = mp_call_function_n_kw(obj, n_args, 0, args);
        nlr_pop();
        proxy_convert_mp_to_js_obj_cside(member, out);
    } else {
        proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
    }
    external_call_depth_dec();
}

__attribute__((export_name("proxy_c_to_js_has_attr")))
bool proxy_c_to_js_has_attr(uint32_t c_ref, const char *attr_in) {
    mp_obj_t obj = proxy_c_get_obj(c_ref);
    qstr attr = qstr_from_str(attr_in);
    if (mp_obj_is_dict_or_ordereddict(obj)) {
        mp_map_t *map = mp_obj_dict_get_map(obj);
        mp_map_elem_t *elem = mp_map_lookup(map, MP_OBJ_NEW_QSTR(attr), MP_MAP_LOOKUP);
        return elem != NULL;
    } else {
        mp_obj_t dest[2];
        mp_load_method_protected(obj, attr, dest, true);
        return dest[0] != MP_OBJ_NULL;
    }
}

__attribute__((export_name("proxy_c_to_js_lookup_attr")))
void proxy_c_to_js_lookup_attr(uint32_t c_ref, const char *attr_in,
                               uint32_t *out) {
    external_call_depth_inc();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t obj = proxy_c_get_obj(c_ref);
        qstr attr = qstr_from_str(attr_in);
        mp_obj_t member;
        if (mp_obj_is_dict_or_ordereddict(obj)) {
            mp_obj_dict_t *self = MP_OBJ_TO_PTR(obj);
            mp_map_elem_t *elem = mp_map_lookup(&self->map, MP_OBJ_NEW_QSTR(attr), MP_MAP_LOOKUP);
            member = (elem == NULL) ? mp_const_undefined : elem->value;
        } else {
            member = mp_load_attr(obj, attr);
        }
        nlr_pop();
        proxy_convert_mp_to_js_obj_cside(member, out);
    } else {
        proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
    }
    external_call_depth_dec();
}

static bool proxy_c_to_js_store_helper(uint32_t c_ref, const char *attr_in,
                                       uint32_t *value_in) {
    external_call_depth_inc();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t obj = proxy_c_get_obj(c_ref);
        qstr attr = qstr_from_str(attr_in);
        mp_obj_t value = MP_OBJ_NULL;
        if (value_in != NULL) {
            value = proxy_convert_js_to_mp_obj_cside(value_in);
        }
        if (mp_obj_is_dict_or_ordereddict(obj)) {
            if (value == MP_OBJ_NULL) {
                mp_obj_dict_delete(obj, MP_OBJ_NEW_QSTR(attr));
            } else {
                mp_obj_dict_store(obj, MP_OBJ_NEW_QSTR(attr), value);
            }
        } else {
            mp_store_attr(obj, attr, value);
        }
        nlr_pop();
        external_call_depth_dec();
        return true;
    } else {
        external_call_depth_dec();
        return false;
    }
}

__attribute__((export_name("proxy_c_to_js_store_attr")))
bool proxy_c_to_js_store_attr(uint32_t c_ref, const char *attr_in,
                              uint32_t *value_in) {
    return proxy_c_to_js_store_helper(c_ref, attr_in, value_in);
}

__attribute__((export_name("proxy_c_to_js_delete_attr")))
bool proxy_c_to_js_delete_attr(uint32_t c_ref, const char *attr_in) {
    return proxy_c_to_js_store_helper(c_ref, attr_in, NULL);
}

__attribute__((export_name("proxy_c_to_js_get_type")))
uint32_t proxy_c_to_js_get_type(uint32_t c_ref) {
    mp_obj_t obj = proxy_c_get_obj(c_ref);
    const mp_obj_type_t *type = mp_obj_get_type(obj);
    if (type == &mp_type_tuple) {
        return 1;
    } else if (type == &mp_type_list) {
        return 2;
    } else if (type == &mp_type_dict) {
        return 3;
    } else {
        return 4;
    }
}

__attribute__((export_name("proxy_c_to_js_get_array")))
void proxy_c_to_js_get_array(uint32_t c_ref, uint32_t *out) {
    mp_obj_t obj = proxy_c_get_obj(c_ref);
    size_t len;
    mp_obj_t *items;
    mp_obj_get_array(obj, &len, &items);
    out[0] = len;
    out[1] = (uintptr_t)items;
}

__attribute__((export_name("proxy_c_to_js_get_dict")))
void proxy_c_to_js_get_dict(uint32_t c_ref, uint32_t *out) {
    mp_obj_t obj = proxy_c_get_obj(c_ref);
    mp_map_t *map = mp_obj_dict_get_map(obj);
    out[0] = map->alloc;
    out[1] = (uintptr_t)map->table;
}

__attribute__((export_name("proxy_c_to_js_get_iter")))
uint32_t proxy_c_to_js_get_iter(uint32_t c_ref) {
    mp_obj_t obj = proxy_c_get_obj(c_ref);
    mp_obj_t iter = mp_getiter(obj, NULL);
    return proxy_c_add_obj(iter);
}

__attribute__((export_name("proxy_c_to_js_iternext")))
bool proxy_c_to_js_iternext(uint32_t c_ref, uint32_t *out) {
    external_call_depth_inc();
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t obj = proxy_c_get_obj(c_ref);
        mp_obj_t iter = mp_iternext_allow_raise(obj);
        if (iter == MP_OBJ_STOP_ITERATION) {
            external_call_depth_dec();
            nlr_pop();
            return false;
        }
        nlr_pop();
        proxy_convert_mp_to_js_obj_cside(iter, out);
        external_call_depth_dec();
        return true;
    } else {
        if (mp_obj_is_subclass_fast(
                MP_OBJ_FROM_PTR(((mp_obj_base_t *)nlr.ret_val)->type),
                MP_OBJ_FROM_PTR(&mp_type_StopIteration))) {
            external_call_depth_dec();
            return false;
        } else {
            proxy_convert_mp_to_js_exc_cside(nlr.ret_val, out);
            external_call_depth_dec();
            return true;
        }
    }
}

__attribute__((export_name("proxy_c_free_obj")))
void proxy_c_free_obj_export(uint32_t c_ref) {
    proxy_c_free_obj(c_ref);
}

#endif /* MICROPY_PY_JSFFI */
