// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2023-2024 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/jsffi_imports.h by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT

/*
 * jsffi_imports.h — WASM import declarations for C↔JS FFI.
 *
 * These functions are provided by JavaScript at WASM instantiation time,
 * in the "jsffi" import namespace.  They replace the EM_JS macros used
 * by MicroPython's Emscripten-based webassembly port.
 *
 * The C code calls these synchronously — the WASM runtime routes the
 * call to JS and back, just like WASI's fd_write.
 *
 * All pointer parameters point into WASM linear memory.  JS reads/writes
 * via DataView on memory.buffer.  PVN = 3 × uint32_t (kind, arg0, arg1).
 */

#ifndef FFI_JSFFI_IMPORTS_H
#define FFI_JSFFI_IMPORTS_H

#include <stdint.h>
#include <stdbool.h>

#if MICROPY_PY_JSFFI

/* ------------------------------------------------------------------ */
/* Import attribute                                                    */
/* ------------------------------------------------------------------ */

#define JSFFI_IMPORT __attribute__((import_module("jsffi")))

/* ------------------------------------------------------------------ */
/* Attribute access                                                    */
/* ------------------------------------------------------------------ */

/* Check if a JS object has an attribute.
 * Returns true if attr exists on proxy_js_ref[jsref]. */
JSFFI_IMPORT __attribute__((import_name("has_attr")))
extern bool jsffi_has_attr(int jsref, const char *str, int str_len);

/* Look up an attribute on a JS object.
 * Writes PVN result to out.
 * Returns: 0 = not found, 1 = found, 2 = found and is function. */
JSFFI_IMPORT __attribute__((import_name("lookup_attr")))
extern int jsffi_lookup_attr(int jsref, const char *str, int str_len,
                             uint32_t *out);

/* Set an attribute on a JS object.
 * value points to a PVN triple. */
JSFFI_IMPORT __attribute__((import_name("store_attr")))
extern void jsffi_store_attr(int jsref, const char *str, int str_len,
                             uint32_t *value);

/* ------------------------------------------------------------------ */
/* Function calls                                                      */
/* ------------------------------------------------------------------ */

/* Call a JS function with no arguments.
 * Writes PVN result to out. */
JSFFI_IMPORT __attribute__((import_name("call0")))
extern void jsffi_call0(int jsref, uint32_t *out);

/* Call a JS function with 1 argument.
 * a0 points to a PVN triple.  Writes PVN result to out.
 * via_call: if true, use f.call(this, ...) semantics.
 * Returns 0 on success, nonzero on JS exception. */
JSFFI_IMPORT __attribute__((import_name("call1")))
extern int jsffi_call1(int jsref, int via_call, uint32_t *a0, uint32_t *out);

/* Call a JS function with n positional arguments.
 * args points to n consecutive PVN triples (n * 3 uint32_t).
 * Returns 0 on success, nonzero on JS exception. */
JSFFI_IMPORT __attribute__((import_name("calln")))
extern int jsffi_calln(int jsref, int via_call, int n_args,
                       uint32_t *args, uint32_t *out);

/* Call a JS function with n positional + n_kw keyword arguments.
 * args: n_args PVN triples.
 * kw_keys: n_kw C string pointers.
 * kw_vals: n_kw PVN triples.
 * Returns 0 on success, nonzero on JS exception. */
JSFFI_IMPORT __attribute__((import_name("calln_kw")))
extern int jsffi_calln_kw(int jsref, int via_call, int n_args,
                          uint32_t *args, int n_kw, uint32_t *kw_keys,
                          uint32_t *kw_vals, uint32_t *out);

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

/* Call Reflect.construct(ctor, args) — for Obj.new() idiom.
 * args: n_args PVN triples.  Writes PVN result to out. */
JSFFI_IMPORT __attribute__((import_name("reflect_construct")))
extern void jsffi_reflect_construct(int jsref, int n_args,
                                    uint32_t *args, uint32_t *out);

/* ------------------------------------------------------------------ */
/* Iterator                                                            */
/* ------------------------------------------------------------------ */

/* Call obj[Symbol.iterator]().
 * Writes PVN iterator to out. */
JSFFI_IMPORT __attribute__((import_name("get_iter")))
extern void jsffi_get_iter(int jsref, uint32_t *out);

/* Call iterator.next().
 * Writes PVN value to out.
 * Returns true if value available, false if done. */
JSFFI_IMPORT __attribute__((import_name("iter_next")))
extern int jsffi_iter_next(int jsref, uint32_t *out);

/* ------------------------------------------------------------------ */
/* Subscript                                                           */
/* ------------------------------------------------------------------ */

/* obj[index] — index is a PVN triple.
 * Writes PVN result to out. */
JSFFI_IMPORT __attribute__((import_name("subscr_load")))
extern void jsffi_subscr_load(int jsref, uint32_t *index, uint32_t *out);

/* obj[index] = value — both index and value are PVN triples. */
JSFFI_IMPORT __attribute__((import_name("subscr_store")))
extern void jsffi_subscr_store(int jsref, uint32_t *index, uint32_t *value);

/* ------------------------------------------------------------------ */
/* Reference management                                                */
/* ------------------------------------------------------------------ */

/* Release a JS-side reference slot.
 * Called when Python JsProxy is garbage collected. */
JSFFI_IMPORT __attribute__((import_name("free_ref")))
extern void jsffi_free_ref(int jsref);

/* Check if a Python object (c_ref) already has a live JS-side PyProxy.
 * Returns js_ref if found, -1 if not. */
JSFFI_IMPORT __attribute__((import_name("check_existing")))
extern int jsffi_check_existing(int c_ref);

/* ------------------------------------------------------------------ */
/* Error introspection                                                 */
/* ------------------------------------------------------------------ */

/* Extract name and message from a JS Error object.
 * Writes PVN triples to out_name and out_message. */
JSFFI_IMPORT __attribute__((import_name("get_error_info")))
extern void jsffi_get_error_info(int jsref, uint32_t *out_name,
                                 uint32_t *out_message);

/* ------------------------------------------------------------------ */
/* Proxy creation (round-trip through JS)                              */
/* ------------------------------------------------------------------ */

/* Convert C→JS PVN to a PyProxy on the JS side, then write back a
 * JS→C PVN referencing that PyProxy as a JsProxy.
 * Used by jsffi.create_proxy(). */
JSFFI_IMPORT __attribute__((import_name("create_pyproxy")))
extern void jsffi_create_pyproxy(uint32_t *in_out);

/* Convert C→JS PVN to a JS value (unwrapping PyProxy if needed),
 * then write back a JS→C PVN.
 * Used by jsffi.to_js(). */
JSFFI_IMPORT __attribute__((import_name("to_js")))
extern void jsffi_to_js(uint32_t *in_out);

#endif /* MICROPY_PY_JSFFI */
#endif /* FFI_JSFFI_IMPORTS_H */
