/**
 * library_asmwasm.js — Emscripten library for native WASM code execution
 *
 * When MicroPython's native emitter (emitnwasm.c / asmwasm.c) compiles a
 * @micropython.native or @micropython.viper function, it produces WASM
 * bytecodes in a memory buffer. This library:
 *
 * 1. Wraps those bytecodes into a valid WASM module (adding type, import,
 *    function, and code sections)
 * 2. Compiles synchronously via new WebAssembly.Module()
 * 3. Instantiates with access to the host's linear memory and mp_fun_table
 * 4. Registers the resulting function in Emscripten's function table
 * 5. Returns the table index (which is a valid C function pointer in WASM)
 *
 * The compiled function receives (fun_table_ptr, arg1, arg2, arg3, arg4)
 * as i32 parameters and returns i32. This matches emitnative.c's calling
 * convention for both @native (returns mp_obj_t) and @viper (returns typed).
 */

mergeInto(LibraryManager.library, {

    // ── Helper functions ($ prefix = internal, not exported as C symbols) ──

    $wasm_uleb128: function(val) {
        const bytes = [];
        do {
            let b = val & 0x7F;
            val >>>= 7;
            if (val !== 0) b |= 0x80;
            bytes.push(b);
        } while (val !== 0);
        return new Uint8Array(bytes);
    },

    $wasm_encode_str__deps: ['$wasm_uleb128', '$wasm_concat'],
    $wasm_encode_str: function(s) {
        const bytes = new TextEncoder().encode(s);
        return wasm_concat([wasm_uleb128(bytes.length), bytes]);
    },

    $wasm_concat: function(arrays) {
        let total = 0;
        for (const a of arrays) total += a.length;
        const result = new Uint8Array(total);
        let offset = 0;
        for (const a of arrays) {
            result.set(a, offset);
            offset += a.length;
        }
        return result;
    },

    // ── Main compile function ──

    mp_wasm_compile_native__deps: ['$wasm_uleb128', '$wasm_encode_str', '$wasm_concat'],
    mp_wasm_compile_native: function(code_ptr, code_len) {
        try {
            const code = Module.HEAPU8.slice(code_ptr, code_ptr + code_len);

            // Build a minimal WASM module around the function body.
            // Function signature: (i32, i32, i32, i32, i32) -> i32
            // Params: fun_table_ptr, arg1, arg2, arg3, arg4

            const parts = [];

            // ── Magic + Version ──
            parts.push(new Uint8Array([0x00, 0x61, 0x73, 0x6D])); // \0asm
            parts.push(new Uint8Array([0x01, 0x00, 0x00, 0x00])); // version 1

            // ── Type Section (section 1) ──
            const typePayload = new Uint8Array([
                0x01,       // 1 type entry
                0x60,       // func type
                0x05,       // 5 params
                0x7F, 0x7F, 0x7F, 0x7F, 0x7F, // all i32
                0x01,       // 1 result
                0x7F,       // i32
            ]);
            parts.push(new Uint8Array([0x01])); // section id
            parts.push(wasm_uleb128(typePayload.length));
            parts.push(typePayload);

            // ── Import Section (section 2) ──
            // Import host's linear memory
            const importPayload = wasm_concat([
                new Uint8Array([0x01]),              // 1 import
                wasm_encode_str('env'),
                wasm_encode_str('memory'),
                new Uint8Array([0x02, 0x00, 0x01]),  // memory, min=0, max=1
            ]);
            parts.push(new Uint8Array([0x02]));
            parts.push(wasm_uleb128(importPayload.length));
            parts.push(importPayload);

            // ── Function Section (section 3) ──
            const funcPayload = new Uint8Array([0x01, 0x00]); // 1 func, type 0
            parts.push(new Uint8Array([0x03]));
            parts.push(wasm_uleb128(funcPayload.length));
            parts.push(funcPayload);

            // ── Export Section (section 7) ──
            const exportPayload = wasm_concat([
                new Uint8Array([0x01]),          // 1 export
                wasm_encode_str('f'),
                new Uint8Array([0x00, 0x00]),    // function, index 0
            ]);
            parts.push(new Uint8Array([0x07]));
            parts.push(wasm_uleb128(exportPayload.length));
            parts.push(exportPayload);

            // ── Code Section (section 10) ──
            const bodySize = wasm_uleb128(code.length);
            const codePayload = wasm_concat([
                new Uint8Array([0x01]),     // 1 function body
                bodySize,
                code,
            ]);
            parts.push(new Uint8Array([0x0A]));
            parts.push(wasm_uleb128(codePayload.length));
            parts.push(codePayload);

            // Assemble the complete module
            const wasmBytes = wasm_concat(parts);

            // Compile synchronously
            const wasmModule = new WebAssembly.Module(wasmBytes);

            // Instantiate with the host's linear memory
            const instance = new WebAssembly.Instance(wasmModule, {
                env: {
                    memory: Module.asm.memory || Module.wasmMemory,
                },
            });

            // Get the compiled function
            const nativeFn = instance.exports.f;

            // Register in Emscripten's function table
            const idx = addFunction(nativeFn, 'iiiiii');
            return idx;

        } catch (e) {
            if (typeof console !== 'undefined') {
                console.error('mp_wasm_compile_native failed:', e.message || e);
            }
            return 0;
        }
    },

    // ── Call cached native function (fallback when addFunction unavailable) ──

    mp_wasm_call_native__deps: [],
    mp_wasm_call_native: function(cache_idx, fun_table, a1, a2, a3, a4) {
        const idx = (-cache_idx) - 1;
        if (!Module._wasmNativeFns || idx >= Module._wasmNativeFns.length) return 0;
        return Module._wasmNativeFns[idx](fun_table, a1, a2, a3, a4);
    },
});
