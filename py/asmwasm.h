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
#ifndef MICROPY_INCLUDED_PY_ASMWASM_H
#define MICROPY_INCLUDED_PY_ASMWASM_H

#include "py/asmbase.h"
#include "py/misc.h"
#include "py/persistentcode.h"

// ---- WASM binary format opcodes ----
// Reference: WebAssembly spec §5.4 (Instruction encoding)
// Verified against wasi-sdk clang 21 / llvm-objdump disassembly

// Control flow
#define WASM_OP_UNREACHABLE     0x00
#define WASM_OP_NOP             0x01
#define WASM_OP_BLOCK           0x02
#define WASM_OP_LOOP            0x03
#define WASM_OP_IF              0x04
#define WASM_OP_ELSE            0x05
#define WASM_OP_END             0x0B
#define WASM_OP_BR              0x0C
#define WASM_OP_BR_IF           0x0D
#define WASM_OP_BR_TABLE        0x0E
#define WASM_OP_RETURN          0x0F

// Call
#define WASM_OP_CALL            0x10
#define WASM_OP_CALL_INDIRECT   0x11

// Parametric
#define WASM_OP_DROP            0x1A
#define WASM_OP_SELECT          0x1B

// Variable access
#define WASM_OP_LOCAL_GET       0x20
#define WASM_OP_LOCAL_SET       0x21
#define WASM_OP_LOCAL_TEE       0x22
#define WASM_OP_GLOBAL_GET      0x23
#define WASM_OP_GLOBAL_SET      0x24

// Memory load
#define WASM_OP_I32_LOAD        0x28    // align=2, offset (memarg)
#define WASM_OP_I32_LOAD8_S     0x2C
#define WASM_OP_I32_LOAD8_U     0x2D    // align=0, offset
#define WASM_OP_I32_LOAD16_S    0x2E    // align=1, offset
#define WASM_OP_I32_LOAD16_U    0x2F

// Memory store
#define WASM_OP_I32_STORE       0x36    // align=2, offset
#define WASM_OP_I32_STORE8      0x3A    // align=0, offset
#define WASM_OP_I32_STORE16     0x3B    // align=1, offset

// Constants
#define WASM_OP_I32_CONST       0x41
#define WASM_OP_I64_CONST       0x42

// Comparison
#define WASM_OP_I32_EQZ         0x45
#define WASM_OP_I32_EQ          0x46
#define WASM_OP_I32_NE          0x47
#define WASM_OP_I32_LT_S        0x48
#define WASM_OP_I32_LT_U        0x49
#define WASM_OP_I32_GT_S        0x4A
#define WASM_OP_I32_GT_U        0x4B
#define WASM_OP_I32_LE_S        0x4C
#define WASM_OP_I32_LE_U        0x4D
#define WASM_OP_I32_GE_S        0x4E
#define WASM_OP_I32_GE_U        0x4F

// Arithmetic (i32)
#define WASM_OP_I32_ADD         0x6A
#define WASM_OP_I32_SUB         0x6B
#define WASM_OP_I32_MUL         0x6C
#define WASM_OP_I32_AND         0x71
#define WASM_OP_I32_OR          0x72
#define WASM_OP_I32_XOR         0x73
#define WASM_OP_I32_SHL         0x74
#define WASM_OP_I32_SHR_S       0x75
#define WASM_OP_I32_SHR_U       0x76

// Block types
#define WASM_BLOCKTYPE_VOID     0x40
#define WASM_BLOCKTYPE_I32      0x7F
#define WASM_BLOCKTYPE_I64      0x7E
#define WASM_BLOCKTYPE_F32      0x7D
#define WASM_BLOCKTYPE_F64      0x7C

// Marker opcodes for the JS rewriter (not real WASM opcodes).
// The C emitter uses these to mark label positions and branch targets.
// library_asmwasm.js transforms them into proper block/loop/end nesting.
#define WASM_MARKER_LABEL     0xFD  // followed by uleb128 label_idx
#define WASM_MARKER_BR        0xFE  // followed by uleb128 label_idx (unconditional)
#define WASM_MARKER_BR_IF     0xFF  // followed by uleb128 label_idx (conditional, value on stack)
// Note: 0xFD-0xFF are in the "reserved" range of the WASM spec and won't
// appear in valid WASM output. The JS rewriter replaces all markers before
// passing to WebAssembly.Module().

// Section IDs
#define WASM_SECTION_TYPE       1
#define WASM_SECTION_IMPORT     2
#define WASM_SECTION_FUNCTION   3
#define WASM_SECTION_TABLE      4
#define WASM_SECTION_MEMORY     5
#define WASM_SECTION_GLOBAL     6
#define WASM_SECTION_EXPORT     7
#define WASM_SECTION_START      8
#define WASM_SECTION_ELEMENT    9
#define WASM_SECTION_CODE       10
#define WASM_SECTION_DATA       11

// Value types
#define WASM_TYPE_I32           0x7F
#define WASM_TYPE_I64           0x7E
#define WASM_TYPE_F32           0x7D
#define WASM_TYPE_F64           0x7C
#define WASM_TYPE_FUNC          0x60

// ---- WASM virtual registers ----
// WASM is a stack machine with locals. We map MicroPython's virtual registers
// to WASM local indices. The function signature provides params 0..N-1, and
// additional locals start at index N.
//
// For native/viper functions, the calling convention is:
//   param 0: fun_table (pointer to mp_fun_table in linear memory)
//   param 1: arg1 (first Python argument)
//   param 2: arg2
//   param 3: arg3
//   params + extra locals for temporaries and callee-saved values

// WASM params match mp_call_fun_t: (self_in, n_args, n_kw, args)
// REG_PARENT_ARG_1 = param 0 (self_in). emitnative.c loads REG_FUN_TABLE
// from self_in's context. The args are extracted from params, not passed directly.
#define ASM_WASM_REG_FUN_TABLE  0   // local 0: param 0 (self_in), then reloaded as fun_table
#define ASM_WASM_REG_ARG_1      1   // local 1: param 1 (n_args)
#define ASM_WASM_REG_ARG_2      2   // local 2: param 2 (n_kw)
#define ASM_WASM_REG_ARG_3      3   // local 3: param 3 (args)
#define ASM_WASM_REG_ARG_4      4   // local 4: extra local (scratch)
#define ASM_WASM_REG_TEMP0      5   // local 5: scratch
#define ASM_WASM_REG_TEMP1      6   // local 6: scratch
#define ASM_WASM_REG_TEMP2      7   // local 7: scratch
#define ASM_WASM_REG_LOCAL_1    8   // local 8: callee-saved
#define ASM_WASM_REG_LOCAL_2    9   // local 9: callee-saved
#define ASM_WASM_REG_LOCAL_3    10  // local 10: callee-saved
#define ASM_WASM_REG_RET        5   // aliases TEMP0 (return value accumulator)
#define ASM_WASM_NUM_REGS       11  // total locals needed

// Number of extra locals beyond the 4 params
#define ASM_WASM_NUM_PARAMS     4
#define ASM_WASM_NUM_EXTRA_LOCALS (ASM_WASM_NUM_REGS - ASM_WASM_NUM_PARAMS)

// ---- Maximum nesting depth for blocks/loops ----
#define ASM_WASM_MAX_BLOCK_DEPTH 32

// ---- Assembler state ----
typedef struct _asm_wasm_t {
    mp_asm_base_t base;

    // Block nesting for structured control flow.
    // Each entry tracks: is this a block (forward branch target) or loop
    // (backward branch target), and which label it corresponds to.
    uint16_t block_depth;
    struct {
        uint16_t label;     // MicroPython label this block maps to
        uint8_t  is_loop;   // 1 = loop (backward), 0 = block (forward)
    } block_stack[ASM_WASM_MAX_BLOCK_DEPTH];

    // Track which labels are backward targets (loop headers) vs forward.
    // Populated during COMPUTE pass, used during EMIT pass.
    uint8_t *label_is_loop;     // array[max_num_labels], 1 if backward target
    uint8_t *label_is_forward;  // array[max_num_labels], 1 if forward target

    // Number of locals the function needs (params + extra + user locals)
    uint16_t num_locals;

    // Offset where the code section body starts (after section header).
    // Used to patch the body size at end of pass.
    size_t code_body_offset;

    // Offset where the function body starts (after local declarations).
    // Used to patch the function body size at end of pass.
    size_t func_body_offset;
} asm_wasm_t;

// ---- Core assembler functions ----
void asm_wasm_init(asm_wasm_t *as, size_t max_num_labels);
void asm_wasm_free(asm_wasm_t *as, bool free_code);
void asm_wasm_start_pass(asm_wasm_t *as, uint pass);
bool asm_wasm_end_pass(asm_wasm_t *as);
void asm_wasm_entry(asm_wasm_t *as, int num_locals);
void asm_wasm_exit(asm_wasm_t *as);

// Low-level byte emission
void asm_wasm_emit_byte(asm_wasm_t *as, uint8_t b);
void asm_wasm_emit_uleb128(asm_wasm_t *as, uint32_t val);
void asm_wasm_emit_sleb128(asm_wasm_t *as, int32_t val);

// Instruction emission
void asm_wasm_op_local_get(asm_wasm_t *as, uint local_idx);
void asm_wasm_op_local_set(asm_wasm_t *as, uint local_idx);
void asm_wasm_op_local_tee(asm_wasm_t *as, uint local_idx);
void asm_wasm_op_i32_const(asm_wasm_t *as, int32_t val);
void asm_wasm_op_i32_load(asm_wasm_t *as, uint align, uint offset);
void asm_wasm_op_i32_load8_u(asm_wasm_t *as, uint offset);
void asm_wasm_op_i32_load16_s(asm_wasm_t *as, uint offset);
void asm_wasm_op_i32_store(asm_wasm_t *as, uint align, uint offset);
void asm_wasm_op_i32_store8(asm_wasm_t *as, uint offset);
void asm_wasm_op_i32_store16(asm_wasm_t *as, uint offset);
void asm_wasm_op_binop(asm_wasm_t *as, uint8_t opcode);
void asm_wasm_op_call_indirect(asm_wasm_t *as, uint type_idx, uint table_idx);

// Control flow
void asm_wasm_op_block(asm_wasm_t *as, uint8_t blocktype);
void asm_wasm_op_loop(asm_wasm_t *as, uint8_t blocktype);
void asm_wasm_op_br(asm_wasm_t *as, uint depth);
void asm_wasm_op_br_if(asm_wasm_t *as, uint depth);
void asm_wasm_op_end(asm_wasm_t *as);
void asm_wasm_op_return(asm_wasm_t *as);

// High-level control flow: translates label-based jumps to structured flow
void asm_wasm_jump(asm_wasm_t *as, uint label);
void asm_wasm_jump_if_reg_zero(asm_wasm_t *as, uint reg, uint label);
void asm_wasm_jump_if_reg_nonzero(asm_wasm_t *as, uint reg, uint label);
void asm_wasm_jump_if_reg_eq(asm_wasm_t *as, uint reg1, uint reg2, uint label);
void asm_wasm_label_assign(asm_wasm_t *as, uint label);

// Indirect call via mp_fun_table (reg_fun_table[idx])
void asm_wasm_call_ind(asm_wasm_t *as, uint fun_table_idx, uint reg_temp);

// Direct call to setjmp (bypasses mp_fun_table for Emscripten compatibility)
void asm_wasm_call_setjmp(asm_wasm_t *as);

// Memory access with offset (word_offset in words, not bytes)
void asm_wasm_ldr_reg_reg_offset(asm_wasm_t *as, uint reg_dest, uint reg_base, uint word_offset);
void asm_wasm_str_reg_reg_offset(asm_wasm_t *as, uint reg_src, uint reg_base, uint word_offset);
void asm_wasm_ldrh_reg_reg_offset(asm_wasm_t *as, uint reg_dest, uint reg_base, uint halfword_offset);

// Move immediate into reg (emit local.set with i32.const)
void asm_wasm_mov_reg_imm(asm_wasm_t *as, uint reg_dest, int32_t imm);

// Move local address (for local variable pointer)
void asm_wasm_mov_reg_local_addr(asm_wasm_t *as, uint reg_dest, uint local_num);

// Move PC-relative (label address)
void asm_wasm_mov_reg_pcrel(asm_wasm_t *as, uint reg_dest, uint label);

// ---- GENERIC ASM API ----
// These macros are used by emitnative.c to generate architecture-independent
// native code. Each expands to the WASM equivalent.

#if defined(GENERIC_ASM_API) && GENERIC_ASM_API

#define ASM_WORD_SIZE (4)

#define REG_RET     ASM_WASM_REG_RET
#define REG_ARG_1   ASM_WASM_REG_ARG_1
#define REG_ARG_2   ASM_WASM_REG_ARG_2
#define REG_ARG_3   ASM_WASM_REG_ARG_3
#define REG_ARG_4   ASM_WASM_REG_ARG_4

#define REG_TEMP0   ASM_WASM_REG_TEMP0
#define REG_TEMP1   ASM_WASM_REG_TEMP1
#define REG_TEMP2   ASM_WASM_REG_TEMP2

#define REG_LOCAL_1 ASM_WASM_REG_LOCAL_1
#define REG_LOCAL_2 ASM_WASM_REG_LOCAL_2
#define REG_LOCAL_3 ASM_WASM_REG_LOCAL_3
#define REG_LOCAL_NUM (3)

#define REG_FUN_TABLE ASM_WASM_REG_FUN_TABLE

#define ASM_T               asm_wasm_t
#define ASM_END_PASS        asm_wasm_end_pass
#define ASM_ENTRY           asm_wasm_entry
#define ASM_EXIT            asm_wasm_exit

// ---- Control flow ----
// WASM uses structured control flow. Forward jumps map to block+br_if,
// backward jumps map to loop+br_if. The asm_wasm_jump* functions handle
// the translation from flat label-based jumps.
#define ASM_JUMP            asm_wasm_jump
#define ASM_JUMP_IF_REG_ZERO(as, reg, label, bool_test) \
    asm_wasm_jump_if_reg_zero(as, reg, label)
#define ASM_JUMP_IF_REG_NONZERO(as, reg, label, bool_test) \
    asm_wasm_jump_if_reg_nonzero(as, reg, label)
#define ASM_JUMP_IF_REG_EQ(as, reg1, reg2, label) \
    asm_wasm_jump_if_reg_eq(as, reg1, reg2, label)
#define ASM_JUMP_REG(as, reg) \
    do { \
        asm_wasm_op_local_get(as, reg); \
        asm_wasm_op_return(as); \
    } while (0)
// Emscripten SUPPORT_LONGJMP=wasm cannot handle indirect calls to setjmp
// (storing &setjmp in mp_fun_table triggers "Indirect use of setjmp is not
// supported"). For the setjmp entry, we emit a direct call instead.
// mp_fun_table[MP_F_SETJMP] is set to NULL for WASM builds (nativeglue.c).
#define ASM_CALL_IND(as, idx) \
    do { \
        if ((idx) == MP_F_SETJMP) { \
            asm_wasm_call_setjmp(as); \
        } else { \
            asm_wasm_call_ind(as, idx, ASM_WASM_REG_TEMP2); \
        } \
    } while (0)

// ---- Register moves ----
#define ASM_MOV_LOCAL_REG(as, local_num, reg) \
    do { \
        asm_wasm_op_local_get(as, reg); \
        asm_wasm_op_local_set(as, ASM_WASM_REG_LOCAL_1 + (local_num)); \
    } while (0)
#define ASM_MOV_REG_IMM(as, reg_dest, imm) \
    asm_wasm_mov_reg_imm(as, reg_dest, imm)
#define ASM_MOV_REG_LOCAL(as, reg_dest, local_num) \
    do { \
        asm_wasm_op_local_get(as, ASM_WASM_REG_LOCAL_1 + (local_num)); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)
#define ASM_MOV_REG_REG(as, reg_dest, reg_src) \
    do { \
        asm_wasm_op_local_get(as, reg_src); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)
#define ASM_MOV_REG_LOCAL_ADDR(as, reg_dest, local_num) \
    asm_wasm_mov_reg_local_addr(as, reg_dest, local_num)
#define ASM_MOV_REG_PCREL(as, reg_dest, label) \
    asm_wasm_mov_reg_pcrel(as, reg_dest, label)

// ---- Bitwise and arithmetic ----
// WASM is stack-based, so "dest = dest OP src" becomes:
//   local.get dest; local.get src; OP; local.set dest
#define ASM_NOT_REG(as, reg_dest) \
    do { \
        asm_wasm_op_local_get(as, reg_dest); \
        asm_wasm_op_i32_const(as, -1); \
        asm_wasm_op_binop(as, WASM_OP_I32_XOR); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)
#define ASM_NEG_REG(as, reg_dest) \
    do { \
        asm_wasm_op_i32_const(as, 0); \
        asm_wasm_op_local_get(as, reg_dest); \
        asm_wasm_op_binop(as, WASM_OP_I32_SUB); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)

// Binary ops: dest = dest OP src
#define ASM_WASM_BINOP(as, reg_dest, reg_src, op) \
    do { \
        asm_wasm_op_local_get(as, reg_dest); \
        asm_wasm_op_local_get(as, reg_src); \
        asm_wasm_op_binop(as, op); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)

#define ASM_LSL_REG_REG(as, reg_dest, reg_shift)  ASM_WASM_BINOP(as, reg_dest, reg_shift, WASM_OP_I32_SHL)
#define ASM_LSR_REG_REG(as, reg_dest, reg_shift)  ASM_WASM_BINOP(as, reg_dest, reg_shift, WASM_OP_I32_SHR_U)
#define ASM_ASR_REG_REG(as, reg_dest, reg_shift)  ASM_WASM_BINOP(as, reg_dest, reg_shift, WASM_OP_I32_SHR_S)
#define ASM_OR_REG_REG(as, reg_dest, reg_src)     ASM_WASM_BINOP(as, reg_dest, reg_src, WASM_OP_I32_OR)
#define ASM_XOR_REG_REG(as, reg_dest, reg_src)    ASM_WASM_BINOP(as, reg_dest, reg_src, WASM_OP_I32_XOR)
#define ASM_AND_REG_REG(as, reg_dest, reg_src)    ASM_WASM_BINOP(as, reg_dest, reg_src, WASM_OP_I32_AND)
#define ASM_ADD_REG_REG(as, reg_dest, reg_src)    ASM_WASM_BINOP(as, reg_dest, reg_src, WASM_OP_I32_ADD)
#define ASM_SUB_REG_REG(as, reg_dest, reg_src)    ASM_WASM_BINOP(as, reg_dest, reg_src, WASM_OP_I32_SUB)
#define ASM_MUL_REG_REG(as, reg_dest, reg_src)    ASM_WASM_BINOP(as, reg_dest, reg_src, WASM_OP_I32_MUL)

// ---- Memory access ----
// Load word from [base + 0]
#define ASM_LOAD_REG_REG(as, reg_dest, reg_base) \
    do { \
        asm_wasm_op_local_get(as, reg_base); \
        asm_wasm_op_i32_load(as, 2, 0); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)
// Load word from [base + word_offset * 4]
#define ASM_LOAD_REG_REG_OFFSET(as, reg_dest, reg_base, word_offset) \
    asm_wasm_ldr_reg_reg_offset(as, reg_dest, reg_base, word_offset)
// Load byte from [base + 0]
#define ASM_LOAD8_REG_REG(as, reg_dest, reg_base) \
    do { \
        asm_wasm_op_local_get(as, reg_base); \
        asm_wasm_op_i32_load8_u(as, 0); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)
// Load halfword (signed) from [base + 0]
#define ASM_LOAD16_REG_REG(as, reg_dest, reg_base) \
    do { \
        asm_wasm_op_local_get(as, reg_base); \
        asm_wasm_op_i32_load16_s(as, 0); \
        asm_wasm_op_local_set(as, reg_dest); \
    } while (0)
// Load halfword with offset
#define ASM_LOAD16_REG_REG_OFFSET(as, reg_dest, reg_base, uint16_offset) \
    asm_wasm_ldrh_reg_reg_offset(as, reg_dest, reg_base, uint16_offset)
// Load word (alias)
#define ASM_LOAD32_REG_REG(as, reg_dest, reg_base) \
    ASM_LOAD_REG_REG(as, reg_dest, reg_base)

// Store word to [base + 0]
#define ASM_STORE_REG_REG(as, reg_src, reg_base) \
    do { \
        asm_wasm_op_local_get(as, reg_base); \
        asm_wasm_op_local_get(as, reg_src); \
        asm_wasm_op_i32_store(as, 2, 0); \
    } while (0)
// Store word to [base + word_offset * 4]
#define ASM_STORE_REG_REG_OFFSET(as, reg_src, reg_base, word_offset) \
    asm_wasm_str_reg_reg_offset(as, reg_src, reg_base, word_offset)
// Store byte to [base + 0]
#define ASM_STORE8_REG_REG(as, reg_src, reg_base) \
    do { \
        asm_wasm_op_local_get(as, reg_base); \
        asm_wasm_op_local_get(as, reg_src); \
        asm_wasm_op_i32_store8(as, 0); \
    } while (0)
// Store halfword to [base + 0]
#define ASM_STORE16_REG_REG(as, reg_src, reg_base) \
    do { \
        asm_wasm_op_local_get(as, reg_base); \
        asm_wasm_op_local_get(as, reg_src); \
        asm_wasm_op_i32_store16(as, 0); \
    } while (0)
// Store word (alias)
#define ASM_STORE32_REG_REG(as, reg_src, reg_base) \
    ASM_STORE_REG_REG(as, reg_src, reg_base)

#endif // GENERIC_ASM_API

#endif // MICROPY_INCLUDED_PY_ASMWASM_H
