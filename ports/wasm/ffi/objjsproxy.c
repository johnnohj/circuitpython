// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/objjsproxy.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT

/*
 * objjsproxy.c — JsProxy type: Python wrapper around a JS object.
 *
 * Adapted from MicroPython's ports/webassembly/objjsproxy.c.
 * Changes for WASM-dist port:
 *   - EM_JS calls replaced with jsffi_* WASM imports
 *   - Async/thenable generator wrapping simplified (Phase 4)
 */

#include <stdio.h>
#include <stdlib.h>

#include "py/objmodule.h"
#include "py/runtime.h"
#include "ffi/proxy_c.h"
#include "ffi/jsffi_imports.h"

#if MICROPY_PY_JSFFI

/* ------------------------------------------------------------------ */
/* JsProxy table — maps js_ref → JsProxy mp_obj_t                     */
/* ------------------------------------------------------------------ */

static mp_obj_t *jsproxy_table = NULL;
static size_t jsproxy_table_len = 0;

/* ------------------------------------------------------------------ */
/* JsProxy print                                                       */
/* ------------------------------------------------------------------ */

static void jsproxy_print(const mp_print_t *print, mp_obj_t self_in,
                          mp_print_kind_t kind) {
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<JsProxy %d>", self->ref);
}

/* ------------------------------------------------------------------ */
/* JsProxy __call__                                                    */
/* ------------------------------------------------------------------ */

static mp_obj_t jsproxy_call(mp_obj_t self_in, size_t n_args, size_t n_kw,
                             const mp_obj_t *args) {
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(self_in);
    mp_arg_check_num(n_args, n_kw, 0, MP_OBJ_FUN_ARGS_MAX, true);

    if (n_kw != 0) {
        uint32_t key[n_kw];
        uint32_t value[PVN * n_kw];
        for (size_t i = 0; i < n_kw; ++i) {
            key[i] = (uintptr_t)mp_obj_str_get_str(args[n_args + i * 2]);
            proxy_convert_mp_to_js_obj_cside(args[n_args + i * 2 + 1],
                                             &value[i * PVN]);
        }
        uint32_t out[PVN];
        if (n_args == 0) {
            jsffi_calln_kw(self->ref, self->bind_to_self, 0, NULL,
                           n_kw, key, value, out);
        } else {
            uint32_t value_args[PVN * n_args];
            for (size_t i = 0; i < n_args; ++i) {
                proxy_convert_mp_to_js_obj_cside(args[i],
                                                 &value_args[i * PVN]);
            }
            jsffi_calln_kw(self->ref, self->bind_to_self, n_args, value_args,
                           n_kw, key, value, out);
        }
        return proxy_convert_js_to_mp_obj_cside(out);
    }

    if (n_args == 0) {
        uint32_t out[PVN];
        jsffi_call0(self->ref, out);
        return proxy_convert_js_to_mp_obj_cside(out);
    } else if (n_args == 1) {
        uint32_t arg0[PVN];
        uint32_t out[PVN];
        proxy_convert_mp_to_js_obj_cside(args[0], arg0);
        jsffi_call1(self->ref, self->bind_to_self, arg0, out);
        return proxy_convert_js_to_mp_obj_cside(out);
    } else {
        uint32_t value[PVN * n_args];
        for (size_t i = 0; i < n_args; ++i) {
            proxy_convert_mp_to_js_obj_cside(args[i], &value[i * PVN]);
        }
        uint32_t out[PVN];
        jsffi_calln(self->ref, self->bind_to_self, n_args, value, out);
        return proxy_convert_js_to_mp_obj_cside(out);
    }
}

/* ------------------------------------------------------------------ */
/* JsProxy __eq__                                                      */
/* ------------------------------------------------------------------ */

static mp_obj_t jsproxy_binary_op(mp_binary_op_t op, mp_obj_t lhs_in,
                                  mp_obj_t rhs_in) {
    if (!mp_obj_is_type(rhs_in, &mp_type_jsproxy)) {
        return MP_OBJ_NULL;
    }
    mp_obj_jsproxy_t *lhs = MP_OBJ_TO_PTR(lhs_in);
    mp_obj_jsproxy_t *rhs = MP_OBJ_TO_PTR(rhs_in);
    switch (op) {
        case MP_BINARY_OP_EQUAL:
            return mp_obj_new_bool(lhs->ref == rhs->ref);
        default:
            return MP_OBJ_NULL;
    }
}

/* ------------------------------------------------------------------ */
/* JsProxy __del__                                                     */
/* ------------------------------------------------------------------ */

static mp_obj_t jsproxy___del__(mp_obj_t self_in) {
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(self_in);
    jsproxy_table[self->ref] = MP_OBJ_NULL;
    jsffi_free_ref(self->ref);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(jsproxy___del___obj, jsproxy___del__);

/* ------------------------------------------------------------------ */
/* JsProxy .new (Reflect.construct)                                    */
/* ------------------------------------------------------------------ */

static mp_obj_t jsproxy_reflect_construct(size_t n_args,
                                          const mp_obj_t *args) {
    int arg0 = mp_obj_jsproxy_get_ref(args[0]);
    n_args -= 1;
    args += 1;
    uint32_t args_conv[PVN * n_args];
    for (size_t i = 0; i < n_args; ++i) {
        proxy_convert_mp_to_js_obj_cside(args[i], &args_conv[i * PVN]);
    }
    uint32_t out[PVN];
    jsffi_reflect_construct(arg0, n_args, args_conv, out);
    return proxy_convert_js_to_mp_obj_cside(out);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR(jsproxy_reflect_construct_obj, 1,
                                   jsproxy_reflect_construct);

/* ------------------------------------------------------------------ */
/* JsProxy __getitem__ / __setitem__                                   */
/* ------------------------------------------------------------------ */

static mp_obj_t jsproxy_subscr(mp_obj_t self_in, mp_obj_t index,
                               mp_obj_t value) {
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(self_in);
    if (value == MP_OBJ_SENTINEL) {
        uint32_t idx[PVN], out[PVN];
        proxy_convert_mp_to_js_obj_cside(index, idx);
        jsffi_subscr_load(self->ref, idx, out);
        return proxy_convert_js_to_mp_obj_cside(out);
    } else if (value == MP_OBJ_NULL) {
        return MP_OBJ_NULL; // delete not supported
    } else {
        uint32_t idx[PVN], val[PVN];
        proxy_convert_mp_to_js_obj_cside(index, idx);
        proxy_convert_mp_to_js_obj_cside(value, val);
        jsffi_subscr_store(self->ref, idx, val);
        return mp_const_none;
    }
}

/* ------------------------------------------------------------------ */
/* JsProxy __getattr__ / __setattr__                                   */
/* ------------------------------------------------------------------ */

static void mp_obj_jsproxy_attr(mp_obj_t self_in, qstr attr,
                                mp_obj_t *dest) {
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(self_in);
    if (dest[0] == MP_OBJ_NULL) {
        /* Load attribute. */
        if (attr == MP_QSTR___del__) {
            dest[0] = MP_OBJ_FROM_PTR(&jsproxy___del___obj);
            dest[1] = self_in;
            return;
        }

        const char *attr_str = qstr_str(attr);
        uint32_t out[PVN];
        int lookup_ret = jsffi_lookup_attr(self->ref, attr_str,
                                           strlen(attr_str), out);
        if (lookup_ret != 0) {
            dest[0] = proxy_convert_js_to_mp_obj_cside(out);
            if (lookup_ret == 2) {
                /* JS method — bind to self for correct 'this'. */
                dest[1] = self_in;
                ((mp_obj_jsproxy_t *)MP_OBJ_TO_PTR(dest[0]))->bind_to_self = true;
            }
        } else if (attr == MP_QSTR_new) {
            dest[0] = MP_OBJ_FROM_PTR(&jsproxy_reflect_construct_obj);
            dest[1] = self_in;
        }
    } else if (dest[1] == MP_OBJ_NULL) {
        /* Delete attribute — no-op. */
    } else {
        /* Store attribute. */
        const char *attr_str = qstr_str(attr);
        uint32_t value[PVN];
        proxy_convert_mp_to_js_obj_cside(dest[1], value);
        jsffi_store_attr(self->ref, attr_str, strlen(attr_str), value);
        dest[0] = MP_OBJ_NULL;
    }
}

/* ------------------------------------------------------------------ */
/* JsProxy iterator                                                    */
/* ------------------------------------------------------------------ */

typedef struct _jsproxy_it_t {
    mp_obj_base_t base;
    mp_fun_1_t iternext;
    mp_obj_jsproxy_t *iter;
} jsproxy_it_t;

static mp_obj_t jsproxy_it_iternext(mp_obj_t self_in) {
    jsproxy_it_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t out[PVN];
    if (jsffi_iter_next(self->iter->ref, out)) {
        return proxy_convert_js_to_mp_obj_cside(out);
    } else {
        return MP_OBJ_STOP_ITERATION;
    }
}

static mp_obj_t jsproxy_new_it(mp_obj_t self_in,
                               mp_obj_iter_buf_t *iter_buf) {
    assert(sizeof(jsproxy_it_t) <= sizeof(mp_obj_iter_buf_t));
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(self_in);
    jsproxy_it_t *o = (jsproxy_it_t *)iter_buf;
    o->base.type = &mp_type_polymorph_iter;
    o->iternext = jsproxy_it_iternext;
    uint32_t out[PVN];
    jsffi_get_iter(self->ref, out);
    o->iter = MP_OBJ_TO_PTR(proxy_convert_js_to_mp_obj_cside(out));
    return MP_OBJ_FROM_PTR(o);
}

/* ------------------------------------------------------------------ */
/* JsProxy __iter__ — dispatch to iterator or generator                */
/* ------------------------------------------------------------------ */

static mp_obj_t jsproxy_getiter(mp_obj_t self_in,
                                mp_obj_iter_buf_t *iter_buf) {
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(self_in);
    if (jsffi_has_attr(self->ref, "then", 4)) {
        /* JS thenable/Promise — Phase 4 will add proper generator wrapping.
         * For now, just iterate as a regular object. */
    }
    return jsproxy_new_it(self_in, iter_buf);
}

/* ------------------------------------------------------------------ */
/* JsProxy type definition                                             */
/* ------------------------------------------------------------------ */

MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_jsproxy,
    MP_QSTR_JsProxy,
    MP_TYPE_FLAG_ITER_IS_GETITER,
    print, jsproxy_print,
    call, jsproxy_call,
    binary_op, jsproxy_binary_op,
    attr, mp_obj_jsproxy_attr,
    subscr, jsproxy_subscr,
    iter, jsproxy_getiter
    );

/* ------------------------------------------------------------------ */
/* JsProxy init / new / get                                            */
/* ------------------------------------------------------------------ */

void mp_obj_jsproxy_init(void) {
    jsproxy_table = NULL;
    jsproxy_table_len = 0;
    MP_STATE_PORT(jsproxy_global_this) =
        mp_obj_new_jsproxy(JSPROXY_REF_GLOBAL_THIS);
}

MP_REGISTER_ROOT_POINTER(mp_obj_t jsproxy_global_this);

mp_obj_t mp_obj_new_jsproxy(int ref) {
    assert(ref >= (int)jsproxy_table_len || jsproxy_table[ref] == MP_OBJ_NULL);

    mp_obj_jsproxy_t *o = mp_obj_malloc_with_finaliser(mp_obj_jsproxy_t,
                                                        &mp_type_jsproxy);
    o->ref = ref;
    o->bind_to_self = false;

    if ((size_t)ref >= jsproxy_table_len) {
        size_t new_len = MAX(16, (size_t)ref * 2);
        jsproxy_table = realloc(jsproxy_table, new_len * sizeof(mp_obj_t));
        for (size_t i = jsproxy_table_len; i < new_len; ++i) {
            jsproxy_table[i] = MP_OBJ_NULL;
        }
        jsproxy_table_len = new_len;
    }
    jsproxy_table[ref] = MP_OBJ_FROM_PTR(o);
    return MP_OBJ_FROM_PTR(o);
}

mp_obj_t mp_obj_get_jsproxy(int ref) {
    assert(ref < (int)jsproxy_table_len && jsproxy_table[ref] != MP_OBJ_NULL);
    return jsproxy_table[ref];
}

/* ------------------------------------------------------------------ */
/* globalThis attribute handler                                        */
/* ------------------------------------------------------------------ */

void mp_obj_jsproxy_global_this_attr(qstr attr, mp_obj_t *dest) {
    if (dest[0] == MP_OBJ_NULL) {
        const char *attr_str = qstr_str(attr);
        uint32_t out[PVN];
        if (jsffi_lookup_attr(JSPROXY_REF_GLOBAL_THIS, attr_str,
                              strlen(attr_str), out)) {
            dest[0] = proxy_convert_js_to_mp_obj_cside(out);
        }
    } else if (dest[1] == MP_OBJ_NULL) {
        /* Delete — no-op. */
    } else {
        const char *attr_str = qstr_str(attr);
        uint32_t value[PVN];
        proxy_convert_mp_to_js_obj_cside(dest[1], value);
        jsffi_store_attr(JSPROXY_REF_GLOBAL_THIS, attr_str,
                         strlen(attr_str), value);
        dest[0] = MP_OBJ_NULL;
    }
}

#endif /* MICROPY_PY_JSFFI */
