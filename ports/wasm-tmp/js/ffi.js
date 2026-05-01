/**
 * jsffi.js — JS-side FFI for CircuitPython WASM.
 *
 * Provides:
 *   - proxy_js_ref table: JS objects indexed by js_ref
 *   - PVN ↔ JS conversion functions
 *   - WASM import implementations for the "jsffi" namespace
 *   - PyProxy class: wraps Python c_ref for JS callers
 *
 * This module replaces the EM_JS macros used by MicroPython's
 * Emscripten-based webassembly port.  Instead of inline JS in C,
 * we provide these functions as WASM imports at instantiation time.
 *
 * Adapted from MicroPython's proxy_js.js.
 * Copyright (c) 2023-2024 Damien P. George (MicroPython)
 * Adapted for CircuitPython WASM-dist port.
 */

/* ------------------------------------------------------------------ */
/* Constants — must match proxy_c.h                                    */
/* ------------------------------------------------------------------ */

// C→JS kinds (mp_obj_t → JS)
const PROXY_KIND_MP_EXCEPTION = -1;
const PROXY_KIND_MP_NULL      = 0;
const PROXY_KIND_MP_NONE      = 1;
const PROXY_KIND_MP_BOOL      = 2;
const PROXY_KIND_MP_INT       = 3;
const PROXY_KIND_MP_FLOAT     = 4;
const PROXY_KIND_MP_STR       = 5;
const PROXY_KIND_MP_CALLABLE  = 6;
const PROXY_KIND_MP_GENERATOR = 7;
const PROXY_KIND_MP_OBJECT    = 8;
const PROXY_KIND_MP_JSPROXY   = 9;
const PROXY_KIND_MP_EXISTING  = 10;

// JS→C kinds (JS → mp_obj_t)
const PROXY_KIND_JS_UNDEFINED       = 0;
const PROXY_KIND_JS_NULL            = 1;
const PROXY_KIND_JS_BOOLEAN         = 2;
const PROXY_KIND_JS_INTEGER         = 3;
const PROXY_KIND_JS_DOUBLE          = 4;
const PROXY_KIND_JS_STRING          = 5;
const PROXY_KIND_JS_OBJECT_EXISTING = 6;
const PROXY_KIND_JS_OBJECT          = 7;
const PROXY_KIND_JS_PYPROXY         = 8;

// Fixed references
const PROXY_JS_REF_NUM_STATIC = 2;

/* ------------------------------------------------------------------ */
/* State — set after WASM instantiation                                */
/* ------------------------------------------------------------------ */

let _memory = null;
let _exports = null;
const _decoder = new TextDecoder();
const _encoder = new TextEncoder();

// JS object reference table (js_ref → JS object)
let proxy_js_ref = [null, undefined]; // [0]=globalThis set in init, [1]=undefined
let proxy_js_ref_map = new Map();     // JS object → js_ref
let proxy_js_ref_next = PROXY_JS_REF_NUM_STATIC;

// PyProxy weak refs for deduplication
let proxy_js_pyproxy_map = new Map(); // c_ref → WeakRef<PyProxy>

/* ------------------------------------------------------------------ */
/* Initialization                                                      */
/* ------------------------------------------------------------------ */

export function jsffi_init(instance) {
    _memory = instance.exports.memory;
    _exports = instance.exports;
    proxy_js_ref[0] = globalThis;
    proxy_js_ref_map.set(globalThis, 0);
}

/* ------------------------------------------------------------------ */
/* JS reference management                                             */
/* ------------------------------------------------------------------ */

function proxy_js_add_obj(obj) {
    // Search for free slot
    for (let i = proxy_js_ref_next; i < proxy_js_ref.length; i++) {
        if (proxy_js_ref[i] === undefined) {
            proxy_js_ref[i] = obj;
            proxy_js_ref_map.set(obj, i);
            proxy_js_ref_next = i + 1;
            return i;
        }
    }
    // No free slot, append
    const id = proxy_js_ref.length;
    proxy_js_ref.push(obj);
    proxy_js_ref_map.set(obj, id);
    proxy_js_ref_next = id + 1;
    return id;
}

/* ------------------------------------------------------------------ */
/* Memory helpers                                                      */
/* ------------------------------------------------------------------ */

function readU32(ptr) {
    return new DataView(_memory.buffer).getUint32(ptr, true);
}

function writeU32(ptr, val) {
    new DataView(_memory.buffer).setUint32(ptr, val, true);
}

function readI32(ptr) {
    return new DataView(_memory.buffer).getInt32(ptr, true);
}

function writeI32(ptr, val) {
    new DataView(_memory.buffer).setInt32(ptr, val, true);
}

function readF64(ptr) {
    return new DataView(_memory.buffer).getFloat64(ptr, true);
}

function writeF64(ptr, val) {
    new DataView(_memory.buffer).setFloat64(ptr, val, true);
}

function readString(ptr, len) {
    return _decoder.decode(new Uint8Array(_memory.buffer, ptr, len));
}

function writeStringToMalloc(str) {
    const bytes = _encoder.encode(str);
    const ptr = _exports.malloc(bytes.length + 1);
    new Uint8Array(_memory.buffer, ptr, bytes.length).set(bytes);
    new Uint8Array(_memory.buffer)[ptr + bytes.length] = 0;
    return { ptr, len: bytes.length };
}

/* ------------------------------------------------------------------ */
/* PVN conversion: JS → C (write PVN triple to WASM memory at `out`)   */
/* ------------------------------------------------------------------ */

function convertJsToPvn(jsObj, out) {
    if (jsObj === undefined) {
        writeU32(out, PROXY_KIND_JS_UNDEFINED);
    } else if (jsObj === null) {
        writeU32(out, PROXY_KIND_JS_NULL);
    } else if (typeof jsObj === 'boolean') {
        writeU32(out, PROXY_KIND_JS_BOOLEAN);
        writeU32(out + 4, jsObj ? 1 : 0);
    } else if (typeof jsObj === 'number') {
        if (Number.isInteger(jsObj) && jsObj >= -2147483648 && jsObj <= 2147483647) {
            writeU32(out, PROXY_KIND_JS_INTEGER);
            writeI32(out + 4, jsObj);
        } else {
            writeU32(out, PROXY_KIND_JS_DOUBLE);
            writeF64(out + 4, jsObj);
        }
    } else if (typeof jsObj === 'string') {
        const { ptr, len } = writeStringToMalloc(jsObj);
        writeU32(out, PROXY_KIND_JS_STRING);
        writeU32(out + 4, len);
        writeU32(out + 8, ptr);
    } else if (jsObj instanceof PyProxy) {
        writeU32(out, PROXY_KIND_JS_PYPROXY);
        writeU32(out + 4, jsObj._c_ref);
    } else {
        // Check if this JS object already has a js_ref
        const existing = proxy_js_ref_map.get(jsObj);
        if (existing !== undefined) {
            writeU32(out, PROXY_KIND_JS_OBJECT_EXISTING);
            writeU32(out + 4, existing);
        } else {
            const ref = proxy_js_add_obj(jsObj);
            writeU32(out, PROXY_KIND_JS_OBJECT);
            writeU32(out + 4, ref);
        }
    }
}

/* ------------------------------------------------------------------ */
/* PVN conversion: C → JS (read PVN triple from WASM memory)           */
/* ------------------------------------------------------------------ */

function convertPvnToJs(ptr) {
    const kind = readI32(ptr);
    switch (kind) {
        case PROXY_KIND_MP_EXCEPTION: {
            const len = readU32(ptr + 4);
            const strPtr = readU32(ptr + 8);
            const msg = readString(strPtr, len);
            _exports.free(strPtr);
            const sep = msg.indexOf('\x04');
            const errType = sep >= 0 ? msg.slice(0, sep) : 'PythonError';
            const errMsg = sep >= 0 ? msg.slice(sep + 1) : msg;
            throw new PythonError(errType, errMsg);
        }
        case PROXY_KIND_MP_NULL:
            return undefined;
        case PROXY_KIND_MP_NONE:
            return null;
        case PROXY_KIND_MP_BOOL:
            return readU32(ptr + 4) !== 0;
        case PROXY_KIND_MP_INT:
            return readI32(ptr + 4);
        case PROXY_KIND_MP_FLOAT:
            return readF64(ptr + 4);
        case PROXY_KIND_MP_STR: {
            const len = readU32(ptr + 4);
            const strPtr = readU32(ptr + 8);
            return readString(strPtr, len);
        }
        case PROXY_KIND_MP_CALLABLE: {
            const c_ref = readU32(ptr + 4);
            return makeCallablePyProxy(c_ref);
        }
        case PROXY_KIND_MP_GENERATOR:
        case PROXY_KIND_MP_OBJECT: {
            const c_ref = readU32(ptr + 4);
            return new PyProxy(c_ref);
        }
        case PROXY_KIND_MP_JSPROXY: {
            const js_ref = readU32(ptr + 4);
            return proxy_js_ref[js_ref];
        }
        case PROXY_KIND_MP_EXISTING: {
            const js_ref = readU32(ptr + 4);
            return proxy_js_ref[js_ref];
        }
        default:
            console.warn('[jsffi] unknown PVN kind:', kind);
            return undefined;
    }
}

/* ------------------------------------------------------------------ */
/* PythonError — wraps Python exceptions for JS                        */
/* ------------------------------------------------------------------ */

class PythonError extends Error {
    constructor(type, message) {
        super(message);
        this.name = type;
        this.pythonType = type;
    }
}

/* ------------------------------------------------------------------ */
/* PyProxy — wraps a Python object (c_ref) for JS callers              */
/* ------------------------------------------------------------------ */

/**
 * PyProxy wraps a Python c_ref for JS callers.
 *
 * For CALLABLE kind, we return a callable function (via JS Proxy trap)
 * so JS code can do `pyproxy(arg1, arg2)` naturally.  The _c_ref
 * property identifies the Python object in proxy_c_ref.
 */
class PyProxy {
    constructor(c_ref) {
        this._c_ref = c_ref;
    }

    call(...args) {
        return pyproxyCall(this._c_ref, args);
    }

    getAttr(attr) {
        const outPtr = _exports.malloc(12);
        const { ptr } = writeStringToMalloc(String(attr));
        _exports.proxy_c_to_js_lookup_attr(this._c_ref, ptr, outPtr);
        const result = convertPvnToJs(outPtr);
        _exports.free(ptr);
        _exports.free(outPtr);
        return result;
    }

    setAttr(attr, value) {
        const valPtr = _exports.malloc(12);
        convertJsToPvn(value, valPtr);
        const { ptr } = writeStringToMalloc(String(attr));
        _exports.proxy_c_to_js_store_attr(this._c_ref, ptr, valPtr);
        _exports.free(ptr);
        _exports.free(valPtr);
    }

    /**
     * Convert this PyProxy to a native JS value.
     * For primitives, returns the value directly.
     * For objects, returns the PyProxy itself (JS can use .call/.getAttr).
     */
    toJs() {
        return this;
    }
}

/**
 * Call a Python callable by c_ref with JS arguments.
 */
function pyproxyCall(c_ref, args) {
    const n = args.length;
    const argsPtr = _exports.malloc(n * 12);
    for (let i = 0; i < n; i++) {
        convertJsToPvn(args[i], argsPtr + i * 12);
    }
    const outPtr = _exports.malloc(12);
    _exports.proxy_c_to_js_call(c_ref, n, argsPtr, outPtr);
    const result = convertPvnToJs(outPtr);
    _exports.free(argsPtr);
    _exports.free(outPtr);
    return result;
}

/**
 * Create a callable PyProxy — a JS function that delegates to Python.
 * This is what JS code receives when Python passes a callback.
 * It's a real function (typeof === 'function') so JS APIs accept it.
 */
function makeCallablePyProxy(c_ref) {
    const fn = function (...args) {
        return pyproxyCall(c_ref, args);
    };
    fn._c_ref = c_ref;
    fn._pyproxy = new PyProxy(c_ref);
    fn.getAttr = (attr) => fn._pyproxy.getAttr(attr);
    fn.setAttr = (attr, val) => fn._pyproxy.setAttr(attr, val);
    fn.toJs = () => fn;
    return fn;
}

/* ------------------------------------------------------------------ */
/* WASM import implementations                                         */
/*                                                                     */
/* These are the 16 functions that C calls via jsffi_* imports.         */
/* They operate on proxy_js_ref and WASM linear memory.                */
/* ------------------------------------------------------------------ */

export function getJsffiImports() {
    return {
        has_attr(jsref, str_ptr, str_len) {
            const base = proxy_js_ref[jsref];
            if (base === undefined || base === null) return 0;
            const attr = readString(str_ptr, str_len);
            return (attr in Object(base)) ? 1 : 0;
        },

        lookup_attr(jsref, str_ptr, str_len, out) {
            const base = proxy_js_ref[jsref];
            const attr = readString(str_ptr, str_len);
            let value = base[attr];
            if (value !== undefined || attr in Object(base)) {
                convertJsToPvn(value, out);
                if (typeof value === 'function' && !('_c_ref' in value)) {
                    return 2; // function
                }
                return 1; // found
            }
            return 0; // not found
        },

        store_attr(jsref, str_ptr, str_len, value_ptr) {
            const attr = readString(str_ptr, str_len);
            const value = convertPvnToJs(value_ptr);
            proxy_js_ref[jsref][attr] = value;
        },

        call0(jsref, out) {
            const f = proxy_js_ref[jsref];
            try {
                const ret = f();
                convertJsToPvn(ret, out);
            } catch (e) {
                convertJsToPvn(e, out);
            }
        },

        call1(jsref, via_call, a0_ptr, out) {
            const a0 = convertPvnToJs(a0_ptr);
            const f = proxy_js_ref[jsref];
            try {
                const ret = via_call ? f.call(a0) : f(a0);
                convertJsToPvn(ret, out);
                return 0;
            } catch (e) {
                convertJsToPvn(e, out);
                return 1;
            }
        },

        calln(jsref, via_call, n_args, args_ptr, out) {
            const f = proxy_js_ref[jsref];
            const a = [];
            for (let i = 0; i < n_args; i++) {
                a.push(convertPvnToJs(args_ptr + i * 12));
            }
            try {
                const ret = via_call ? f.call(...a) : f(...a);
                convertJsToPvn(ret, out);
                return 0;
            } catch (e) {
                convertJsToPvn(e, out);
                return 1;
            }
        },

        calln_kw(jsref, via_call, n_args, args_ptr,
                 n_kw, kw_keys_ptr, kw_vals_ptr, out) {
            const f = proxy_js_ref[jsref];
            const a = [];
            for (let i = 0; i < n_args; i++) {
                a.push(convertPvnToJs(args_ptr + i * 12));
            }
            const kw = {};
            for (let i = 0; i < n_kw; i++) {
                const keyPtr = readU32(kw_keys_ptr + i * 4);
                // Read null-terminated C string
                let end = keyPtr;
                const mem = new Uint8Array(_memory.buffer);
                while (mem[end] !== 0) end++;
                const key = readString(keyPtr, end - keyPtr);
                kw[key] = convertPvnToJs(kw_vals_ptr + i * 12);
            }
            try {
                const ret = via_call ? f.call(...a, kw) : f(...a, kw);
                convertJsToPvn(ret, out);
                return 0;
            } catch (e) {
                convertJsToPvn(e, out);
                return 1;
            }
        },

        reflect_construct(jsref, n_args, args_ptr, out) {
            const f = proxy_js_ref[jsref];
            const a = [];
            for (let i = 0; i < n_args; i++) {
                a.push(convertPvnToJs(args_ptr + i * 12));
            }
            try {
                const ret = Reflect.construct(f, a);
                convertJsToPvn(ret, out);
            } catch (e) {
                convertJsToPvn(e, out);
            }
        },

        get_iter(jsref, out) {
            const obj = proxy_js_ref[jsref];
            try {
                const iter = obj[Symbol.iterator]();
                convertJsToPvn(iter, out);
            } catch (e) {
                convertJsToPvn(undefined, out);
            }
        },

        iter_next(jsref, out) {
            const iter = proxy_js_ref[jsref];
            try {
                const ret = iter.next();
                if (ret.done) {
                    return 0;
                }
                convertJsToPvn(ret.value, out);
                return 1;
            } catch (e) {
                return 0;
            }
        },

        subscr_load(jsref, index_ptr, out) {
            const target = proxy_js_ref[jsref];
            let index = convertPvnToJs(index_ptr);
            // Python negative indexing
            if (typeof index === 'number' && index < 0 && target.length !== undefined) {
                index = target.length + index;
            }
            const ret = target[index];
            convertJsToPvn(ret, out);
        },

        subscr_store(jsref, index_ptr, value_ptr) {
            const target = proxy_js_ref[jsref];
            const index = convertPvnToJs(index_ptr);
            const value = convertPvnToJs(value_ptr);
            target[index] = value;
        },

        free_ref(jsref) {
            if (jsref >= PROXY_JS_REF_NUM_STATIC) {
                proxy_js_ref_map.delete(proxy_js_ref[jsref]);
                proxy_js_ref[jsref] = undefined;
                if (jsref < proxy_js_ref_next) {
                    proxy_js_ref_next = jsref;
                }
            }
        },

        check_existing(c_ref) {
            const wr = proxy_js_pyproxy_map.get(c_ref);
            if (wr) {
                const obj = wr.deref();
                if (obj !== undefined) {
                    const ref = proxy_js_ref_map.get(obj);
                    return ref !== undefined ? ref : -1;
                }
                proxy_js_pyproxy_map.delete(c_ref);
            }
            return -1;
        },

        get_error_info(jsref, out_name, out_message) {
            const error = proxy_js_ref[jsref];
            const name = error && error.name ? String(error.name) : 'Error';
            const message = error && error.message ? String(error.message) : String(error);
            convertJsToPvn(name, out_name);
            convertJsToPvn(message, out_message);
        },

        create_pyproxy(in_out) {
            // Read C→JS PVN, convert to JS (creates PyProxy/callable)
            const jsObj = convertPvnToJs(in_out);
            // Store the PyProxy in proxy_js_ref so Python gets a JsProxy back
            const ref = proxy_js_add_obj(jsObj);
            // Write JS→C PVN: (JS_OBJECT, js_ref)
            writeU32(in_out, PROXY_KIND_JS_OBJECT);
            writeU32(in_out + 4, ref);
            writeU32(in_out + 8, 0);
        },

        to_js(in_out) {
            // Read C→JS PVN, convert to JS value
            const jsObj = convertPvnToJs(in_out);
            // If it's a PyProxy/callable, unwrap via toJs()
            const unwrapped = (jsObj && typeof jsObj.toJs === 'function')
                ? jsObj.toJs()
                : jsObj;
            // Convert back to JS→C PVN
            convertJsToPvn(unwrapped, in_out);
        },
    };
}

/* ---- Exports ---- */

export { PyProxy, PythonError, proxy_js_ref };
