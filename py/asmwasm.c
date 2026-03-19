/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Contributors to the MicroPython project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "py/mpconfig.h"

#if MICROPY_EMIT_WASM

#include "py/mpstate.h"
#include "py/asmwasm.h"

// ---- Low-level byte emission ----

void asm_wasm_emit_byte(asm_wasm_t *as, uint8_t b) {
    uint8_t *c = mp_asm_base_get_cur_to_write_bytes(&as->base, 1);
    if (c != NULL) {
        c[0] = b;
    }
}

static void emit_bytes(asm_wasm_t *as, const uint8_t *buf, size_t len) {
    uint8_t *c = mp_asm_base_get_cur_to_write_bytes(&as->base, len);
    if (c != NULL) {
        memcpy(c, buf, len);
    }
}

void asm_wasm_emit_uleb128(asm_wasm_t *as, uint32_t val) {
    do {
        uint8_t b = val & 0x7F;
        val >>= 7;
        if (val != 0) {
            b |= 0x80;
        }
        asm_wasm_emit_byte(as, b);
    } while (val != 0);
}

void asm_wasm_emit_sleb128(asm_wasm_t *as, int32_t val) {
    bool more = true;
    while (more) {
        uint8_t b = val & 0x7F;
        val >>= 7;
        // sign bit of byte is second high order bit (0x40)
        if ((val == 0 && !(b & 0x40)) || (val == -1 && (b & 0x40))) {
            more = false;
        } else {
            b |= 0x80;
        }
        asm_wasm_emit_byte(as, b);
    }
}

// ---- Instruction emission ----

void asm_wasm_op_local_get(asm_wasm_t *as, uint local_idx) {
    asm_wasm_emit_byte(as, WASM_OP_LOCAL_GET);
    asm_wasm_emit_uleb128(as, local_idx);
}

void asm_wasm_op_local_set(asm_wasm_t *as, uint local_idx) {
    asm_wasm_emit_byte(as, WASM_OP_LOCAL_SET);
    asm_wasm_emit_uleb128(as, local_idx);
}

void asm_wasm_op_local_tee(asm_wasm_t *as, uint local_idx) {
    asm_wasm_emit_byte(as, WASM_OP_LOCAL_TEE);
    asm_wasm_emit_uleb128(as, local_idx);
}

void asm_wasm_op_i32_const(asm_wasm_t *as, int32_t val) {
    asm_wasm_emit_byte(as, WASM_OP_I32_CONST);
    asm_wasm_emit_sleb128(as, val);
}

// Memory access with memarg (alignment + offset, both as uleb128)
void asm_wasm_op_i32_load(asm_wasm_t *as, uint align, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_LOAD);
    asm_wasm_emit_uleb128(as, align);
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_i32_load8_u(asm_wasm_t *as, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_LOAD8_U);
    asm_wasm_emit_uleb128(as, 0);  // align = 0 (byte aligned)
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_i32_load16_s(asm_wasm_t *as, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_LOAD16_S);
    asm_wasm_emit_uleb128(as, 1);  // align = 1 (2-byte aligned)
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_i32_store(asm_wasm_t *as, uint align, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_STORE);
    asm_wasm_emit_uleb128(as, align);
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_i32_store8(asm_wasm_t *as, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_STORE8);
    asm_wasm_emit_uleb128(as, 0);
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_i32_store16(asm_wasm_t *as, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_STORE16);
    asm_wasm_emit_uleb128(as, 1);
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_binop(asm_wasm_t *as, uint8_t opcode) {
    asm_wasm_emit_byte(as, opcode);
}

void asm_wasm_op_call_indirect(asm_wasm_t *as, uint type_idx, uint table_idx) {
    asm_wasm_emit_byte(as, WASM_OP_CALL_INDIRECT);
    asm_wasm_emit_uleb128(as, type_idx);
    asm_wasm_emit_uleb128(as, table_idx);
}

// ---- Control flow instructions ----

void asm_wasm_op_block(asm_wasm_t *as, uint8_t blocktype) {
    asm_wasm_emit_byte(as, WASM_OP_BLOCK);
    asm_wasm_emit_byte(as, blocktype);
}

void asm_wasm_op_loop(asm_wasm_t *as, uint8_t blocktype) {
    asm_wasm_emit_byte(as, WASM_OP_LOOP);
    asm_wasm_emit_byte(as, blocktype);
}

void asm_wasm_op_br(asm_wasm_t *as, uint depth) {
    asm_wasm_emit_byte(as, WASM_OP_BR);
    asm_wasm_emit_uleb128(as, depth);
}

void asm_wasm_op_br_if(asm_wasm_t *as, uint depth) {
    asm_wasm_emit_byte(as, WASM_OP_BR_IF);
    asm_wasm_emit_uleb128(as, depth);
}

void asm_wasm_op_end(asm_wasm_t *as) {
    asm_wasm_emit_byte(as, WASM_OP_END);
}

void asm_wasm_op_return(asm_wasm_t *as) {
    asm_wasm_emit_byte(as, WASM_OP_RETURN);
}

// ---- High-level control flow ----
// Translates MicroPython's flat label-based jumps to WASM structured control
// flow (block/loop/br).
//
// Strategy:
// - During COMPUTE pass, label_assign records label positions. After the pass,
//   we analyze which jumps are backward (target label offset < jump offset) and
//   mark those labels as loop headers.
// - During EMIT pass, label_assign either opens a block (forward) or a loop
//   (backward). Jumps emit br/br_if with the appropriate nesting depth.
//
// This is a simplified approach that works for the patterns emitnative.c
// generates (for-loops, if/else). More complex control flow (e.g., nested
// exception handlers with multiple backward jumps) may need refinement.

void asm_wasm_label_assign(asm_wasm_t *as, uint label) {
    // Base class records offset for this label
    mp_asm_base_label_assign(&as->base, label);

    if (as->base.pass == MP_ASM_PASS_EMIT) {
        if (as->label_is_loop != NULL && as->label_is_loop[label]) {
            // This label is a loop header: emit loop instruction.
            // Backward jumps to this label will use br to re-enter.
            assert(as->block_depth < ASM_WASM_MAX_BLOCK_DEPTH);
            as->block_stack[as->block_depth].label = label;
            as->block_stack[as->block_depth].is_loop = 1;
            as->block_depth++;
            asm_wasm_op_loop(as, WASM_BLOCKTYPE_VOID);
        } else {
            // Forward target: the corresponding block was opened at the jump
            // site. Here at the label we emit the end for that block.
            // (This is handled by the jump functions.)
        }
    }
}

// Find the block depth for a given label (for br operand calculation)
static uint find_block_depth(asm_wasm_t *as, uint label) {
    // Search block stack from top (innermost) to bottom (outermost)
    for (int i = (int)as->block_depth - 1; i >= 0; i--) {
        if (as->block_stack[i].label == label) {
            return as->block_depth - 1 - i;
        }
    }
    // Label not found in block stack — this shouldn't happen if
    // label_assign was called correctly
    assert(0 && "label not found in block stack");
    return 0;
}

void asm_wasm_jump(asm_wasm_t *as, uint label) {
    if (as->base.pass == MP_ASM_PASS_EMIT) {
        if (as->label_is_loop != NULL && as->label_is_loop[label]) {
            // Backward jump: br to the loop block
            uint depth = find_block_depth(as, label);
            asm_wasm_op_br(as, depth);
        } else {
            // Forward jump: br out of enclosing block
            // The block was opened before the jump, and end is at the label.
            uint depth = find_block_depth(as, label);
            asm_wasm_op_br(as, depth);
        }
    } else {
        // COMPUTE pass: record that we jump to this label.
        // If the label has already been assigned (offset < current offset),
        // mark it as a backward target (loop).
        if (as->label_is_loop != NULL &&
            label < as->base.max_num_labels &&
            as->base.label_offsets[label] != (size_t)-1 &&
            as->base.label_offsets[label] <= as->base.code_offset) {
            as->label_is_loop[label] = 1;
        }
        // Emit placeholder bytes (worst case: 2 bytes for br + uleb128)
        asm_wasm_emit_byte(as, WASM_OP_BR);
        asm_wasm_emit_uleb128(as, ASM_WASM_MAX_BLOCK_DEPTH);
    }
}

void asm_wasm_jump_if_reg_zero(asm_wasm_t *as, uint reg, uint label) {
    // Push value, compare with zero, branch if true
    asm_wasm_op_local_get(as, reg);
    asm_wasm_op_i32_const(as, 0);
    asm_wasm_op_binop(as, WASM_OP_I32_EQ);
    if (as->base.pass == MP_ASM_PASS_EMIT) {
        uint depth = find_block_depth(as, label);
        asm_wasm_op_br_if(as, depth);
    } else {
        if (as->label_is_loop != NULL &&
            label < as->base.max_num_labels &&
            as->base.label_offsets[label] != (size_t)-1 &&
            as->base.label_offsets[label] <= as->base.code_offset) {
            as->label_is_loop[label] = 1;
        }
        asm_wasm_emit_byte(as, WASM_OP_BR_IF);
        asm_wasm_emit_uleb128(as, ASM_WASM_MAX_BLOCK_DEPTH);
    }
}

void asm_wasm_jump_if_reg_nonzero(asm_wasm_t *as, uint reg, uint label) {
    // Push value, branch if nonzero (WASM br_if branches on nonzero)
    asm_wasm_op_local_get(as, reg);
    if (as->base.pass == MP_ASM_PASS_EMIT) {
        uint depth = find_block_depth(as, label);
        asm_wasm_op_br_if(as, depth);
    } else {
        if (as->label_is_loop != NULL &&
            label < as->base.max_num_labels &&
            as->base.label_offsets[label] != (size_t)-1 &&
            as->base.label_offsets[label] <= as->base.code_offset) {
            as->label_is_loop[label] = 1;
        }
        asm_wasm_emit_byte(as, WASM_OP_BR_IF);
        asm_wasm_emit_uleb128(as, ASM_WASM_MAX_BLOCK_DEPTH);
    }
}

void asm_wasm_jump_if_reg_eq(asm_wasm_t *as, uint reg1, uint reg2, uint label) {
    asm_wasm_op_local_get(as, reg1);
    asm_wasm_op_local_get(as, reg2);
    asm_wasm_op_binop(as, WASM_OP_I32_EQ);
    if (as->base.pass == MP_ASM_PASS_EMIT) {
        uint depth = find_block_depth(as, label);
        asm_wasm_op_br_if(as, depth);
    } else {
        if (as->label_is_loop != NULL &&
            label < as->base.max_num_labels &&
            as->base.label_offsets[label] != (size_t)-1 &&
            as->base.label_offsets[label] <= as->base.code_offset) {
            as->label_is_loop[label] = 1;
        }
        asm_wasm_emit_byte(as, WASM_OP_BR_IF);
        asm_wasm_emit_uleb128(as, ASM_WASM_MAX_BLOCK_DEPTH);
    }
}

// ---- Direct call (by function index in the WASM module) ----
// Used for functions that Emscripten cannot call indirectly (e.g., setjmp).
void asm_wasm_op_call(asm_wasm_t *as, uint func_idx) {
    asm_wasm_emit_byte(as, WASM_OP_CALL);
    asm_wasm_emit_uleb128(as, func_idx);
}

// ---- Indirect call via mp_fun_table ----
// mp_fun_table is an array of function pointers in linear memory.
// REG_FUN_TABLE holds the base address. To call entry [idx]:
//   local.get fun_table_reg    (base address)
//   i32.const idx * 4          (byte offset)
//   i32.add                    (compute &table[idx])
//   i32.load                   (load function pointer)
//   call_indirect              (call through table)
//
// SPECIAL CASE: setjmp must be called directly, not through call_indirect.
// Emscripten's SUPPORT_LONGJMP=wasm requires static visibility of setjmp
// calls to insert WASM exception handling. The ASM_CALL_IND macro in
// asmwasm.h detects MP_F_SETJMP and routes to asm_wasm_call_setjmp().

void asm_wasm_call_ind(asm_wasm_t *as, uint fun_table_idx, uint reg_temp) {
    // Load function address from mp_fun_table
    asm_wasm_op_local_get(as, ASM_WASM_REG_FUN_TABLE);
    asm_wasm_op_i32_const(as, fun_table_idx * 4);
    asm_wasm_op_binop(as, WASM_OP_I32_ADD);
    asm_wasm_op_i32_load(as, 2, 0);
    // call_indirect with type signature 0, table 0
    asm_wasm_op_call_indirect(as, 0, 0);
}

// Direct call to setjmp — argument (jmp_buf pointer) should already be
// in REG_ARG_1. Emits a direct `call $setjmp` so Emscripten can see it
// statically and insert the proper WASM exception handling transform.
void asm_wasm_call_setjmp(asm_wasm_t *as) {
    // Push the jmp_buf argument onto the WASM stack
    asm_wasm_op_local_get(as, ASM_WASM_REG_ARG_1);
    // Emit direct call to setjmp.
    // The function index for setjmp in the final WASM module is resolved
    // by the linker — we emit a call to the imported setjmp symbol.
    // In Emscripten, setjmp is a well-known import that the compiler
    // recognizes. We use WASM_OP_CALL with a placeholder index that
    // the linker will resolve.
    // For now, we rely on the fact that emitnative.c's NLR setup will
    // put the jmp_buf address in REG_ARG_1 before calling us.
    asm_wasm_emit_byte(as, WASM_OP_CALL);
    // Emit placeholder function index — the linker resolves "setjmp"
    // to the correct index. During compilation, emcc sees the direct
    // call and applies its setjmp/longjmp transform.
    asm_wasm_emit_uleb128(as, 0); // placeholder, needs linker resolution
}

// ---- Memory access with offset ----

void asm_wasm_ldr_reg_reg_offset(asm_wasm_t *as, uint reg_dest, uint reg_base, uint word_offset) {
    asm_wasm_op_local_get(as, reg_base);
    asm_wasm_op_i32_load(as, 2, word_offset * 4);
    asm_wasm_op_local_set(as, reg_dest);
}

void asm_wasm_str_reg_reg_offset(asm_wasm_t *as, uint reg_src, uint reg_base, uint word_offset) {
    asm_wasm_op_local_get(as, reg_base);
    asm_wasm_op_local_get(as, reg_src);
    asm_wasm_op_i32_store(as, 2, word_offset * 4);
}

void asm_wasm_ldrh_reg_reg_offset(asm_wasm_t *as, uint reg_dest, uint reg_base, uint halfword_offset) {
    asm_wasm_op_local_get(as, reg_base);
    asm_wasm_op_i32_load16_s(as, halfword_offset * 2);
    asm_wasm_op_local_set(as, reg_dest);
}

// ---- Register-immediate move ----

void asm_wasm_mov_reg_imm(asm_wasm_t *as, uint reg_dest, int32_t imm) {
    asm_wasm_op_i32_const(as, imm);
    asm_wasm_op_local_set(as, reg_dest);
}

// ---- Local address ----
// WASM doesn't have address-of for locals (they're not in linear memory).
// For viper code that takes ptr to a local, we'd need to spill to memory.
// For now, emit the local index as an immediate (placeholder).
void asm_wasm_mov_reg_local_addr(asm_wasm_t *as, uint reg_dest, uint local_num) {
    // TODO: Allocate stack frame in linear memory for addressable locals.
    // For now, this is a stub that will need the JS runtime to handle.
    asm_wasm_op_i32_const(as, 0); // placeholder
    asm_wasm_op_local_set(as, reg_dest);
}

// ---- PC-relative label address ----
// WASM doesn't have PC-relative addressing. Labels are branch targets, not
// addresses. For code that needs label addresses (e.g., computed goto),
// we emit 0 as a placeholder.
void asm_wasm_mov_reg_pcrel(asm_wasm_t *as, uint reg_dest, uint label) {
    asm_wasm_op_i32_const(as, 0); // placeholder
    asm_wasm_op_local_set(as, reg_dest);
}

// ---- Assembler lifecycle ----

void asm_wasm_init(asm_wasm_t *as, size_t max_num_labels) {
    memset(as, 0, sizeof(*as));
    mp_asm_base_init(&as->base, max_num_labels);
    as->label_is_loop = m_new0(uint8_t, max_num_labels);
}

void asm_wasm_free(asm_wasm_t *as, bool free_code) {
    if (as->label_is_loop != NULL) {
        m_del(uint8_t, as->label_is_loop, as->base.max_num_labels);
        as->label_is_loop = NULL;
    }
    mp_asm_base_deinit(&as->base, free_code);
}

void asm_wasm_start_pass(asm_wasm_t *as, uint pass) {
    mp_asm_base_start_pass(&as->base, pass);
    as->block_depth = 0;
    if (pass == MP_ASM_PASS_COMPUTE) {
        // Clear loop markers for fresh analysis
        if (as->label_is_loop != NULL) {
            memset(as->label_is_loop, 0, as->base.max_num_labels);
        }
    }
}

bool asm_wasm_end_pass(asm_wasm_t *as) {
    // Close any remaining open blocks
    while (as->block_depth > 0) {
        as->block_depth--;
        asm_wasm_op_end(as);
    }
    // Final end for the function body
    asm_wasm_op_end(as);
    return false; // no need to replay
}

// ---- Entry/exit ----
// In a complete WASM module emitter, entry would emit the module header
// (magic, version, type section, function section) and the code section
// preamble (local declarations). For now, we just emit the function body
// instructions — the JS-side loader wraps them into a valid module.

void asm_wasm_entry(asm_wasm_t *as, int num_locals) {
    as->num_locals = ASM_WASM_NUM_REGS + num_locals;

    // Emit local declarations for the extra locals (beyond params).
    // Format: count of local declaration groups, then (count, type) pairs.
    uint extra_locals = ASM_WASM_NUM_EXTRA_LOCALS + num_locals;
    if (extra_locals > 0) {
        asm_wasm_emit_uleb128(as, 1);           // 1 group of locals
        asm_wasm_emit_uleb128(as, extra_locals); // count
        asm_wasm_emit_byte(as, WASM_TYPE_I32);   // type: i32
    } else {
        asm_wasm_emit_uleb128(as, 0);           // 0 local groups
    }

    as->func_body_offset = as->base.code_offset;
}

void asm_wasm_exit(asm_wasm_t *as) {
    // Push return value from REG_RET onto the stack
    asm_wasm_op_local_get(as, ASM_WASM_REG_RET);
    asm_wasm_op_return(as);
}

#endif // MICROPY_EMIT_WASM
