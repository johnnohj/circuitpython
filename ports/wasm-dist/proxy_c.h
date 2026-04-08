/*
 * proxy_c.h — C-side proxy reference management and PVN marshaling.
 *
 * Adapted from MicroPython's ports/webassembly/proxy_c.h.
 * PVN (Proxy Value Number) is the 3 × uint32_t interchange format
 * used for all C↔JS type conversions across the WASM import boundary.
 *
 * Two reference systems:
 *   proxy_c_ref (C-side): Python objects exported to JS, indexed by c_ref.
 *     Stored in MP_STATE_PORT(proxy_c_ref) list.
 *   proxy_js_ref (JS-side): JS objects exported to Python, indexed by js_ref.
 *     Stored in a JS array managed by jsffi.js.
 *
 * Copyright (c) 2023-2024 Damien P. George (MicroPython)
 * Adapted for CircuitPython WASM-dist port.
 */

#ifndef WASM_PROXY_C_H
#define WASM_PROXY_C_H

#include "py/obj.h"

#if MICROPY_PY_JSFFI

/* ------------------------------------------------------------------ */
/* PVN (Proxy Value Number) — 3 × uint32_t interchange format          */
/* ------------------------------------------------------------------ */

#define PVN (3)

/* ---- Kinds for C→JS (mp_obj_t → JS value) ---- */
enum {
    PROXY_KIND_MP_EXCEPTION = -1,
    PROXY_KIND_MP_NULL      = 0,
    PROXY_KIND_MP_NONE      = 1,
    PROXY_KIND_MP_BOOL      = 2,
    PROXY_KIND_MP_INT       = 3,
    PROXY_KIND_MP_FLOAT     = 4,
    PROXY_KIND_MP_STR       = 5,
    PROXY_KIND_MP_CALLABLE  = 6,
    PROXY_KIND_MP_GENERATOR = 7,
    PROXY_KIND_MP_OBJECT    = 8,
    PROXY_KIND_MP_JSPROXY   = 9,
    PROXY_KIND_MP_EXISTING  = 10,
};

/* ---- Kinds for JS→C (JS value → mp_obj_t) ---- */
enum {
    PROXY_KIND_JS_UNDEFINED       = 0,
    PROXY_KIND_JS_NULL            = 1,
    PROXY_KIND_JS_BOOLEAN         = 2,
    PROXY_KIND_JS_INTEGER         = 3,
    PROXY_KIND_JS_DOUBLE          = 4,
    PROXY_KIND_JS_STRING          = 5,
    PROXY_KIND_JS_OBJECT_EXISTING = 6,
    PROXY_KIND_JS_OBJECT          = 7,
    PROXY_KIND_JS_PYPROXY         = 8,
};

/* ------------------------------------------------------------------ */
/* Fixed JS reference indices                                          */
/* ------------------------------------------------------------------ */

#define JSPROXY_REF_GLOBAL_THIS  (0)
#define JSPROXY_REF_UNDEFINED    (1)

/* ------------------------------------------------------------------ */
/* JsProxy type — Python wrapper around a JS object                    */
/* ------------------------------------------------------------------ */

typedef struct _mp_obj_jsproxy_t {
    mp_obj_base_t base;
    int ref;           /* Index into proxy_js_ref (JS-side array) */
    bool bind_to_self; /* If true, call via f.call(this) semantics */
} mp_obj_jsproxy_t;

extern const mp_obj_type_t mp_type_jsproxy;
extern const mp_obj_type_t mp_type_JsException;

/* ------------------------------------------------------------------ */
/* C-side API                                                          */
/* ------------------------------------------------------------------ */

/* Initialize proxy reference tables.  Called during cp_init(). */
void proxy_c_init(void);

/* Convert a PVN triple (from JS) to an mp_obj_t. */
mp_obj_t proxy_convert_js_to_mp_obj_cside(uint32_t *value);

/* Convert an mp_obj_t to a PVN triple (for JS). */
void proxy_convert_mp_to_js_obj_cside(mp_obj_t obj, uint32_t *out);

/* Serialize an exception to a PVN triple (kind=EXCEPTION). */
void proxy_convert_mp_to_js_exc_cside(void *exc, uint32_t *out);

/* Add a Python object to proxy_c_ref, return its c_ref index.
 * Used when exporting a Python object to JS. */
size_t proxy_c_add_obj(mp_obj_t obj);

/* Get the Python object at c_ref index. */
mp_obj_t proxy_c_get_obj(uint32_t c_ref);

/* Free a c_ref slot (called when JS releases a PyProxy). */
void proxy_c_free_obj(uint32_t c_ref);

/* Create a new JsProxy wrapping js_ref. */
mp_obj_t mp_obj_new_jsproxy(int ref);

/* Retrieve an existing JsProxy for js_ref. */
mp_obj_t mp_obj_get_jsproxy(int ref);

/* Global this attribute handler. */
void mp_obj_jsproxy_global_this_attr(qstr attr, mp_obj_t *dest);

/* Create a JsException from a JsProxy error object. */
mp_obj_t mp_obj_jsproxy_make_js_exception(mp_obj_t error);

/* ------------------------------------------------------------------ */
/* Inline helpers                                                      */
/* ------------------------------------------------------------------ */

static inline bool mp_obj_is_jsproxy(mp_obj_t o) {
    return mp_obj_get_type(o) == &mp_type_jsproxy;
}

static inline int mp_obj_jsproxy_get_ref(mp_obj_t o) {
    mp_obj_jsproxy_t *self = MP_OBJ_TO_PTR(o);
    return self->ref;
}

/* ------------------------------------------------------------------ */
/* JS→C callback entry points (exported to WASM)                       */
/* ------------------------------------------------------------------ */

/* Call a Python callable from JS.
 * args_value: n_args PVN triples.  out: PVN result. */
void proxy_c_to_js_call(uint32_t c_ref, uint32_t n_args,
                        uint32_t *args_value, uint32_t *out);

/* Attribute access on Python objects from JS. */
bool proxy_c_to_js_has_attr(uint32_t c_ref, const char *attr_in);
void proxy_c_to_js_lookup_attr(uint32_t c_ref, const char *attr_in,
                               uint32_t *out);
bool proxy_c_to_js_store_attr(uint32_t c_ref, const char *attr_in,
                              uint32_t *value_in);
bool proxy_c_to_js_delete_attr(uint32_t c_ref, const char *attr_in);

/* Type introspection and array/dict access for JS. */
uint32_t proxy_c_to_js_get_type(uint32_t c_ref);
void proxy_c_to_js_get_array(uint32_t c_ref, uint32_t *out);
void proxy_c_to_js_get_dict(uint32_t c_ref, uint32_t *out);

/* Iterator bridging. */
uint32_t proxy_c_to_js_get_iter(uint32_t c_ref);
bool proxy_c_to_js_iternext(uint32_t c_ref, uint32_t *out);

#endif /* MICROPY_PY_JSFFI */
#endif /* WASM_PROXY_C_H */
