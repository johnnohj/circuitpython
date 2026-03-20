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

    /**
     * Rewrite flat marker opcodes (LABEL/BR/BR_IF) into valid WASM
     * structured control flow (block/loop/br/br_if/end).
     *
     * Input: function body with marker opcodes 0xFD/0xFE/0xFF
     * Output: valid WASM function body with proper block nesting
     *
     * Algorithm:
     * 1. Scan for all LABEL markers and BR/BR_IF markers
     * 2. Classify each label as forward-target, backward-target, or both
     * 3. For forward labels: wrap the region [first br to label .. label] in a block
     * 4. For backward labels: wrap the region [label .. last br to label] in a loop
     * 5. Replace BR/BR_IF markers with real br/br_if using correct depth
     */
    $wasm_rewrite_control_flow__deps: ['$wasm_uleb128'],
    $wasm_rewrite_control_flow: function(code) {
        const MARKER_LABEL = 0xFD;
        const MARKER_BR    = 0xFE;
        const MARKER_BR_IF = 0xFF;
        const OP_BLOCK  = 0x02, OP_LOOP = 0x03, OP_END = 0x0B;
        const OP_BR = 0x0C, OP_BR_IF = 0x0D, OP_RETURN = 0x0F;
        const BLOCKTYPE_VOID = 0x40;

        // Parse: find all markers and their byte positions
        const labels = {};    // label_idx → { pos, forward: bool, backward: bool }
        const branches = [];  // { pos, label_idx, conditional }
        let i = 0;

        // Skip local declarations at the start of the function body
        // Format: num_groups (uleb128), then for each group: count (uleb128), type (byte)
        function readUleb(arr, off) {
            let val = 0, shift = 0, b;
            do {
                b = arr[off++];
                val |= (b & 0x7F) << shift;
                shift += 7;
            } while (b & 0x80);
            return [val, off];
        }

        // Skip local declarations
        const [numGroups, afterGroups] = readUleb(code, 0);
        i = afterGroups;
        for (let g = 0; g < numGroups; g++) {
            const [_count, afterCount] = readUleb(code, i);
            i = afterCount + 1; // +1 for the type byte
        }
        const localsEnd = i; // instructions start here

        // First pass: find all markers (only in instruction region)
        while (i < code.length) {
            const op = code[i];
            if (op === MARKER_LABEL) {
                const [labelIdx, next] = readUleb(code, i + 1);
                labels[labelIdx] = { pos: i, forward: false, backward: false };
                i = next;
            } else if (op === MARKER_BR || op === MARKER_BR_IF) {
                const [labelIdx, next] = readUleb(code, i + 1);
                branches.push({ pos: i, labelIdx, conditional: op === MARKER_BR_IF });
                i = next;
            } else {
                // Skip regular WASM opcodes (variable length)
                i = wasm_skip_instruction(code, i);
            }
        }

        // Classify labels
        for (const br of branches) {
            const lbl = labels[br.labelIdx];
            if (!lbl) continue;
            if (br.pos < lbl.pos) {
                lbl.forward = true;   // branch before label = forward
            } else {
                lbl.backward = true;  // branch after label = backward
            }
        }

        // Build output: copy code, replacing markers with real instructions.
        // Strategy: for each label position, insert block/loop/end as needed.
        //
        // Simple approach: process the code linearly. At each label marker:
        // - If forward target: we already opened a block earlier, emit end
        // - If backward target: emit loop header
        //
        // At each branch marker: emit br/br_if with correct depth
        //
        // We need to track the block stack to compute br depths.

        // Sort forward labels by position (ascending) to open blocks
        const forwardLabels = Object.entries(labels)
            .filter(([_, l]) => l.forward)
            .map(([idx, l]) => ({ idx: +idx, pos: l.pos }))
            .sort((a, b) => a.pos - b.pos);

        // Build output — copy local declarations verbatim, then rewrite instructions
        const out = [];
        for (let ci = 0; ci < localsEnd; ci++) out.push(code[ci]);

        const blockStack = []; // { labelIdx, type: 'block'|'loop' }

        // Open blocks for all forward labels at the start (outermost = latest position)
        for (let fi = forwardLabels.length - 1; fi >= 0; fi--) {
            blockStack.push({ labelIdx: forwardLabels[fi].idx, type: 'block' });
            out.push(OP_BLOCK, BLOCKTYPE_VOID);
        }

        i = localsEnd;
        while (i < code.length) {
            const op = code[i];

            if (op === MARKER_LABEL) {
                const [labelIdx, next] = readUleb(code, i + 1);
                const lbl = labels[labelIdx];

                // Close forward block if this label is a forward target
                if (lbl.forward) {
                    // Find and close this label's block
                    const stackIdx = blockStack.findIndex(b => b.labelIdx === labelIdx && b.type === 'block');
                    if (stackIdx >= 0) {
                        // Close all blocks from top to this one (they must nest)
                        // Actually, we should only close the innermost matching one.
                        // Since forward blocks are opened outermost=latest, the
                        // first (lowest pos) label should be innermost on the stack.
                        // Pop everything above it and re-push after.
                        const above = blockStack.splice(stackIdx);
                        above.shift(); // remove this label's block
                        out.push(OP_END);
                        // Re-push remaining (shouldn't happen with simple nesting)
                        for (const b of above) blockStack.push(b);
                    }
                }

                // Open loop if this label is a backward target
                if (lbl.backward) {
                    blockStack.push({ labelIdx, type: 'loop' });
                    out.push(OP_LOOP, BLOCKTYPE_VOID);
                }

                i = next;
            } else if (op === MARKER_BR || op === MARKER_BR_IF) {
                const [labelIdx, next] = readUleb(code, i + 1);

                // Find the target on the block stack
                let depth = -1;
                for (let si = blockStack.length - 1; si >= 0; si--) {
                    if (blockStack[si].labelIdx === labelIdx) {
                        depth = blockStack.length - 1 - si;
                        break;
                    }
                }

                if (depth < 0) {
                    // Label not on stack — this is a forward jump past a backward
                    // label's loop end. Use return as fallback.
                    out.push(OP_RETURN);
                } else {
                    const realOp = (op === MARKER_BR_IF) ? OP_BR_IF : OP_BR;
                    out.push(realOp);
                    const depthBytes = wasm_uleb128(depth);
                    for (let di = 0; di < depthBytes.length; di++) out.push(depthBytes[di]);
                }

                i = next;
            } else {
                // Copy regular instruction bytes
                const next = wasm_skip_instruction(code, i);
                for (let ci = i; ci < next; ci++) out.push(code[ci]);
                i = next;
            }
        }

        // Close any remaining open blocks (backward labels' loops)
        while (blockStack.length > 0) {
            blockStack.pop();
            out.push(OP_END);
        }

        return new Uint8Array(out);
    },

    /**
     * Skip one WASM instruction, returning the offset of the next instruction.
     * Handles variable-length encodings for common opcodes.
     */
    $wasm_skip_instruction__deps: [],
    $wasm_skip_instruction: function(code, i) {
        const op = code[i++];
        // Custom marker opcodes (1 uleb128 label index)
        if (op === 0xFD || op === 0xFE || op === 0xFF) {
            while (i < code.length && code[i] & 0x80) i++;
            i++; // final byte of uleb128
            return i;
        }
        // Opcodes with uleb128 immediate(s)
        if ((op >= 0x20 && op <= 0x24) || op === 0x0C || op === 0x0D || op === 0x10) {
            // local.get/set/tee, global.get/set, br, br_if, call — 1 uleb128
            while (i < code.length && code[i] & 0x80) i++;
            i++; // final byte
        } else if (op === 0x41) {
            // i32.const — sleb128
            while (i < code.length && code[i] & 0x80) i++;
            i++;
        } else if (op === 0x42) {
            // i64.const — sleb128
            while (i < code.length && code[i] & 0x80) i++;
            i++;
        } else if (op === 0x11) {
            // call_indirect — 2 uleb128 (type_idx, table_idx)
            while (i < code.length && code[i] & 0x80) i++;
            i++;
            while (i < code.length && code[i] & 0x80) i++;
            i++;
        } else if (op >= 0x28 && op <= 0x3E) {
            // memory ops — 2 uleb128 (align, offset)
            while (i < code.length && code[i] & 0x80) i++;
            i++;
            while (i < code.length && code[i] & 0x80) i++;
            i++;
        } else if (op === 0x02 || op === 0x03 || op === 0x04) {
            // block/loop/if — 1 byte blocktype
            i++;
        }
        // All other opcodes: 0 immediates (single byte)
        return i;
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

    mp_wasm_compile_native__deps: ['$wasm_uleb128', '$wasm_encode_str', '$wasm_concat', '$wasm_rewrite_control_flow', '$wasm_skip_instruction'],
    mp_wasm_compile_native: function(code_ptr, code_len) {
        try {
            let code = Module.HEAPU8.slice(code_ptr, code_ptr + code_len);

            // Rewrite flat marker opcodes into valid WASM structured control flow
            code = wasm_rewrite_control_flow(code);

            // Build a minimal WASM module around the function body.
            // Function signature: (i32, i32, i32, i32, i32) -> i32
            // Params: fun_table_ptr, arg1, arg2, arg3, arg4

            const parts = [];

            // ── Magic + Version ──
            parts.push(new Uint8Array([0x00, 0x61, 0x73, 0x6D])); // \0asm
            parts.push(new Uint8Array([0x01, 0x00, 0x00, 0x00])); // version 1

            // ── Type Section (section 1) ──
            // mp_call_fun_t signature: (self_in, n_args, n_kw, args) -> mp_obj_t
            // All i32 on wasm32
            const typePayload = new Uint8Array([
                0x01,       // 1 type entry
                0x60,       // func type
                0x04,       // 4 params
                0x7F, 0x7F, 0x7F, 0x7F, // all i32
                0x01,       // 1 result
                0x7F,       // i32
            ]);
            parts.push(new Uint8Array([0x01])); // section id
            parts.push(wasm_uleb128(typePayload.length));
            parts.push(typePayload);

            // ── Import Section (section 2) ──
            // Import host's linear memory AND function table so call_indirect works
            const importPayload = wasm_concat([
                new Uint8Array([0x02]),              // 2 imports
                // Import 1: memory
                wasm_encode_str('env'),
                wasm_encode_str('memory'),
                new Uint8Array([0x02, 0x00, 0x01]),  // memory, min=0, max=1
                // Import 2: function table (for call_indirect via mp_fun_table)
                wasm_encode_str('env'),
                wasm_encode_str('__indirect_function_table'),
                new Uint8Array([0x01, 0x70, 0x00, 0x00]),  // table, funcref, min=0, no max
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

            // Instantiate with the host's linear memory and function table
            const memory = Module.wasmMemory || (Module.asm && Module.asm.memory);
            const table = (Module.asm && Module.asm.__indirect_function_table) ||
                          Module.wasmTable;
            const instance = new WebAssembly.Instance(wasmModule, {
                env: {
                    memory: memory,
                    __indirect_function_table: table,
                },
            });

            // Get the compiled function
            const nativeFn = instance.exports.f;

            // Register in Emscripten's function table
            const idx = addFunction(nativeFn, 'iiiii'); // 4 i32 params + i32 return
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
