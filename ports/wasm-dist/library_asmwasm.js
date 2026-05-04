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
     * 3. Detect while-loop patterns (forward-br + backward-label + condition
     *    at bottom) and restructure into top-test loops via code motion
     * 4. For remaining forward labels: wrap in blocks opened at function start
     * 5. Replace BR/BR_IF markers with real br/br_if using correct depth
     *
     * While-loop restructuring:
     *   MicroPython emits bottom-test loops:
     *     br F; label B; <body>; label F; <cond>; br_if B
     *   WASM requires structured control flow, so we restructure to top-test:
     *     block; loop; <cond>; i32.eqz; br_if 1; <body>; br 0; end; end
     *   The condition bytes are moved from bottom to top of the loop.
     */
    $wasm_rewrite_control_flow__deps: ['$wasm_uleb128'],
    $wasm_rewrite_control_flow: function(code) {
        const MARKER_LABEL = 0xFD;
        const MARKER_BR    = 0xFE;
        const MARKER_BR_IF = 0xFF;
        const OP_BLOCK = 0x02, OP_LOOP = 0x03, OP_END = 0x0B;
        const OP_BR = 0x0C, OP_BR_IF = 0x0D, OP_RETURN = 0x0F;
        const OP_I32_EQZ = 0x45;
        const BLOCKTYPE_VOID = 0x40;

        function readUleb(arr, off) {
            let val = 0, shift = 0, b;
            do {
                b = arr[off++];
                val |= (b & 0x7F) << shift;
                shift += 7;
            } while (b & 0x80);
            return [val, off];
        }

        // Skip local declarations at the start of the function body
        let i = 0;
        const [numGroups, afterGroups] = readUleb(code, 0);
        i = afterGroups;
        for (let g = 0; g < numGroups; g++) {
            const [_count, afterCount] = readUleb(code, i);
            i = afterCount + 1; // +1 for the type byte
        }
        const localsEnd = i;

        // ── First pass: find all markers ──
        const labels = {};    // label_idx → { pos, forward, backward }
        const branches = [];  // { pos, labelIdx, conditional, end }
        while (i < code.length) {
            const op = code[i];
            if (op === MARKER_LABEL) {
                const [labelIdx, next] = readUleb(code, i + 1);
                labels[labelIdx] = { pos: i, forward: false, backward: false };
                i = next;
            } else if (op === MARKER_BR || op === MARKER_BR_IF) {
                const [labelIdx, next] = readUleb(code, i + 1);
                branches.push({ pos: i, labelIdx, conditional: op === MARKER_BR_IF, end: next });
                i = next;
            } else {
                i = wasm_skip_instruction(code, i);
            }
        }

        // ── Classify labels ──
        for (const br of branches) {
            const lbl = labels[br.labelIdx];
            if (!lbl) continue;
            if (br.pos < lbl.pos) lbl.forward = true;
            else lbl.backward = true;
        }

        // ── Detect while-loop patterns ──
        // Pattern: unconditional br F (forward) immediately followed by
        // MARKER_LABEL B (backward target), with F between B and back-edge.
        // This is the standard bottom-test while loop that must be
        // restructured into a top-test loop for WASM.
        const whileLoops = {};           // bodyLabelIdx → { condLabelIdx, condBytes, backBranchEnd }
        const whileCondLabels = new Set();  // condition label indices (excluded from forward blocks)
        const whileBodyLabels = new Set();  // body label indices (get block+loop instead of plain loop)
        const whileInitBranches = new Set(); // positions of initial "skip to cond" branches

        for (const br of branches) {
            if (br.conditional) continue;
            const condLbl = labels[br.labelIdx];
            if (!condLbl || !condLbl.forward) continue;

            // Next marker after this br must be MARKER_LABEL for a backward target
            if (br.end >= code.length || code[br.end] !== MARKER_LABEL) continue;
            const [bodyLabelIdx, _bodyLabelEnd] = readUleb(code, br.end + 1);
            const bodyLbl = labels[bodyLabelIdx];
            if (!bodyLbl || !bodyLbl.backward) continue;

            // F must be after B (condition is between body and back-edge)
            if (condLbl.pos <= bodyLbl.pos) continue;

            // Find the conditional backward branch to B after F
            let backBr = null;
            for (const br2 of branches) {
                if (br2.labelIdx === bodyLabelIdx && br2.conditional && br2.pos > condLbl.pos) {
                    backBr = br2;
                    break;
                }
            }
            if (!backBr) continue;

            // Extract condition bytes: after MARKER_LABEL F up to MARKER_BR_IF B
            const [, condStart] = readUleb(code, condLbl.pos + 1);
            const condBytes = code.slice(condStart, backBr.pos);

            whileLoops[bodyLabelIdx] = {
                condLabelIdx: br.labelIdx,
                condBytes: condBytes,
                backBranchEnd: backBr.end,
            };
            whileCondLabels.add(br.labelIdx);
            whileBodyLabels.add(bodyLabelIdx);
            whileInitBranches.add(br.pos);
        }

        // ── Build output ──
        const out = [];
        for (let ci = 0; ci < localsEnd; ci++) out.push(code[ci]);

        const blockStack = []; // { labelIdx, type: 'block'|'loop' }

        // Open forward blocks at function start (excluding while-loop cond labels)
        const forwardLabels = Object.entries(labels)
            .filter(([idx, l]) => l.forward && !whileCondLabels.has(+idx))
            .map(([idx, l]) => ({ idx: +idx, pos: l.pos }))
            .sort((a, b) => a.pos - b.pos);

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

                if (whileBodyLabels.has(labelIdx)) {
                    // ── While-loop body start ──
                    // Emit block (exit target) + loop (back-edge target),
                    // then the condition bytes moved from bottom.
                    const wl = whileLoops[labelIdx];

                    // Exit block: br_if 1 from inside loop exits here
                    blockStack.push({ labelIdx: -labelIdx - 1, type: 'block' });
                    out.push(OP_BLOCK, BLOCKTYPE_VOID);
                    // Loop header: br 0 from body end re-enters here
                    blockStack.push({ labelIdx: labelIdx, type: 'loop' });
                    out.push(OP_LOOP, BLOCKTYPE_VOID);

                    // Emit condition (moved from bottom to top of loop)
                    for (let ci = 0; ci < wl.condBytes.length; ci++) {
                        out.push(wl.condBytes[ci]);
                    }
                    // Invert condition and exit if false:
                    // Original br_if meant "branch if true (continue)".
                    // We need "exit if false" → i32.eqz + br_if 1.
                    out.push(OP_I32_EQZ);
                    out.push(OP_BR_IF);
                    out.push(1); // depth 1 = exit block

                    i = next;

                } else if (whileCondLabels.has(labelIdx)) {
                    // ── While-loop condition label ──
                    // Body is done. Emit back-branch, close loop+block,
                    // skip the original condition + back-branch bytes.
                    let bodyLabelIdx = null;
                    for (const [blIdx, wl] of Object.entries(whileLoops)) {
                        if (wl.condLabelIdx === labelIdx) {
                            bodyLabelIdx = +blIdx;
                            break;
                        }
                    }
                    const wl = whileLoops[bodyLabelIdx];

                    // Back to loop header
                    out.push(OP_BR);
                    out.push(0); // depth 0 = loop
                    // Close loop and exit block
                    out.push(OP_END); // end loop
                    out.push(OP_END); // end exit block

                    // Remove loop and exit block from stack
                    for (let si = blockStack.length - 1; si >= 0; si--) {
                        if (blockStack[si].labelIdx === bodyLabelIdx && blockStack[si].type === 'loop') {
                            blockStack.splice(si, 1);
                            break;
                        }
                    }
                    for (let si = blockStack.length - 1; si >= 0; si--) {
                        if (blockStack[si].labelIdx === -bodyLabelIdx - 1 && blockStack[si].type === 'block') {
                            blockStack.splice(si, 1);
                            break;
                        }
                    }

                    // Skip past original condition + back-branch
                    i = wl.backBranchEnd;

                } else {
                    // ── Normal label ──
                    if (lbl.forward) {
                        const stackIdx = blockStack.findIndex(b => b.labelIdx === labelIdx && b.type === 'block');
                        if (stackIdx >= 0) {
                            const above = blockStack.splice(stackIdx);
                            above.shift();
                            out.push(OP_END);
                            for (const b of above) blockStack.push(b);
                        }
                    }
                    if (lbl.backward) {
                        blockStack.push({ labelIdx, type: 'loop' });
                        out.push(OP_LOOP, BLOCKTYPE_VOID);
                    }
                    i = next;
                }

            } else if (op === MARKER_BR || op === MARKER_BR_IF) {
                const [labelIdx, next] = readUleb(code, i + 1);

                if (whileInitBranches.has(i)) {
                    // Skip the initial "jump to condition" — condition moved to loop top
                    i = next;
                } else {
                    // Normal branch: find target on block stack
                    let depth = -1;
                    for (let si = blockStack.length - 1; si >= 0; si--) {
                        if (blockStack[si].labelIdx === labelIdx) {
                            depth = blockStack.length - 1 - si;
                            break;
                        }
                    }

                    if (depth < 0) {
                        out.push(OP_RETURN);
                    } else {
                        out.push((op === MARKER_BR_IF) ? OP_BR_IF : OP_BR);
                        const depthBytes = wasm_uleb128(depth);
                        for (let di = 0; di < depthBytes.length; di++) out.push(depthBytes[di]);
                    }
                    i = next;
                }

            } else {
                // Copy regular instruction bytes
                const next = wasm_skip_instruction(code, i);
                for (let ci = i; ci < next; ci++) out.push(code[ci]);
                i = next;
            }
        }

        // Close any remaining open blocks
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
            // Type 0: (i32,i32,i32,i32)->i32 — the compiled function signature
            // Type 1: (i32,i32,i32,i32,i32)->i32 — trampoline: (table_idx, a1, a2, a3, a4)->ret
            const typePayload = new Uint8Array([
                0x02,       // 2 type entries
                // Type 0: function signature
                0x60,       // func type
                0x04,       // 4 params
                0x7F, 0x7F, 0x7F, 0x7F, // all i32
                0x01,       // 1 result
                0x7F,       // i32
                // Type 1: trampoline signature
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
            // Import host memory, function table, AND a trampoline function.
            // The trampoline handles call_indirect with correct type signatures —
            // it takes (table_index, arg1, arg2, arg3, arg4) and calls through
            // the host's function table. This avoids type mismatches because
            // the trampoline lives in the host module where all types are known.
            //
            // Types: 0 = (i32,i32,i32,i32)->i32 (function signature)
            //        1 = (i32,i32,i32,i32,i32)->i32 (trampoline signature)
            const importPayload = wasm_concat([
                new Uint8Array([0x03]),              // 3 imports
                // Import 1: memory
                wasm_encode_str('env'),
                wasm_encode_str('memory'),
                new Uint8Array([0x02, 0x00, 0x01]),  // memory, min=0, max=1
                // Import 2: function table
                wasm_encode_str('env'),
                wasm_encode_str('__indirect_function_table'),
                new Uint8Array([0x01, 0x70, 0x00, 0x00]),  // table, funcref, min=0, no max
                // Import 3: trampoline function (call_fun_table)
                wasm_encode_str('env'),
                wasm_encode_str('call_fun_table'),
                new Uint8Array([0x00, 0x01]),        // function, type index 1
            ]);
            parts.push(new Uint8Array([0x02]));
            parts.push(wasm_uleb128(importPayload.length));
            parts.push(importPayload);

            // ── Function Section (section 3) ──
            // Function 1 (index 0 is the imported trampoline): type 0
            const funcPayload = new Uint8Array([0x01, 0x00]); // 1 func, type 0
            parts.push(new Uint8Array([0x03]));
            parts.push(wasm_uleb128(funcPayload.length));
            parts.push(funcPayload);

            // ── Export Section (section 7) ──
            const exportPayload = wasm_concat([
                new Uint8Array([0x01]),          // 1 export
                wasm_encode_str('f'),
                new Uint8Array([0x00, 0x01]),    // function, index 1 (after imported trampoline)
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
            // Trampoline: calls through host function table with correct types.
            // Takes (table_index, arg1, arg2, arg3, arg4) → result.
            // Handles varying arities by always passing 4 args (unused ones ignored).
            function call_fun_table(tbl_idx, a1, a2, a3, a4) {
                const fn = table.get(tbl_idx);
                if (!fn) return 0;
                return fn(a1, a2, a3, a4) | 0;
            }

            const instance = new WebAssembly.Instance(wasmModule, {
                env: {
                    memory: memory,
                    __indirect_function_table: table,
                    call_fun_table: call_fun_table,
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
