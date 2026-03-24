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

void asm_wasm_op_i32_load(asm_wasm_t *as, uint align, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_LOAD);
    asm_wasm_emit_uleb128(as, align);
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_i32_load8_u(asm_wasm_t *as, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_LOAD8_U);
    asm_wasm_emit_uleb128(as, 0);
    asm_wasm_emit_uleb128(as, offset);
}

void asm_wasm_op_i32_load16_s(asm_wasm_t *as, uint offset) {
    asm_wasm_emit_byte(as, WASM_OP_I32_LOAD16_S);
    asm_wasm_emit_uleb128(as, 1);
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

void asm_wasm_op_call(asm_wasm_t *as, uint func_idx) {
    asm_wasm_emit_byte(as, WASM_OP_CALL);
    asm_wasm_emit_uleb128(as, func_idx);
}

// ============================================================================
// Structured control flow
//
// WASM requires block/loop/end constructs — no arbitrary jumps. We translate
// emitnative.c's flat label-based jumps using this scheme:
//
// At function entry, we open one void `block` per forward-target label,
// nested from outermost (last label) to innermost (first label). Each
// forward jump becomes `br N` where N is the depth to the target block.
// At each forward label position, we emit `end` to close that block.
//
// For backward-target labels (loops), we emit `loop` at the label position
// and `end` after the loop body. Backward jumps use `br` to re-enter.
//
// The function body is wrapped in an outer void block so that forward
// branches never need to provide a return value on the stack.
//
// Block nesting (example with 2 forward labels F0, F1 and 1 loop label L0):
//
//   block $F1          ; outermost forward block
//     block $F0        ; inner forward block
//       ... code ...
//       br_if 0        ; jump to F0 (depth 0 = innermost)
//       ... code ...
//       br 1           ; jump to F1 (depth 1 = one up)
//       ... code ...
//     end              ; F0 label position
//     loop $L0         ; loop header
//       ... code ...
//       br_if 0        ; jump back to L0 (depth 0 = this loop)
//     end              ; end of loop body
//     ... code ...
//   end                ; F1 label position
//   local.get $ret
//   return
// ============================================================================

// Helper: record a jump target during COMPUTE pass
static void record_jump_target(asm_wasm_t *as, uint label) {
    if (label >= as->base.max_num_labels) return;
    size_t lbl_off = as->base.label_offsets[label];
    if (as->label_is_loop != NULL &&
        lbl_off != (size_t)-1 &&
        lbl_off <= as->base.code_offset) {
        as->label_is_loop[label] = 1;  // backward target
    }
    if (as->label_is_forward != NULL &&
        (lbl_off == (size_t)-1 ||
         lbl_off > as->base.code_offset)) {
        as->label_is_forward[label] = 1;  // forward target
    }
}

// Find the br depth for a given label in the current block stack
static uint find_block_depth(asm_wasm_t *as, uint label) {
    for (int i = (int)as->block_depth - 1; i >= 0; i--) {
        if (as->block_stack[i].label == label) {
            return as->block_depth - 1 - i;
        }
    }
    // Not found — shouldn't happen
    return 0;
}

void asm_wasm_label_assign(asm_wasm_t *as, uint label) {
    mp_asm_base_label_assign(&as->base, label);
    // Emit a label marker. The JS rewriter uses these to build block structure.
    asm_wasm_emit_byte(as, WASM_MARKER_LABEL);
    asm_wasm_emit_uleb128(as, label);

    #if MICROPY_WASM_COOPERATIVE_YIELD
    // At backward branch targets (loop headers), emit a budget check.
    // This prevents native while-True loops from freezing the browser.
    if (as->label_is_loop != NULL &&
        label < as->base.max_num_labels &&
        as->label_is_loop[label]) {
        asm_wasm_emit_yield_check(as);
    }
    #endif
}

// Emit a jump as a marker (the JS rewriter transforms to br/br_if + blocks)
static void emit_jump_common(asm_wasm_t *as, uint label, bool conditional) {
    asm_wasm_emit_byte(as, conditional ? WASM_MARKER_BR_IF : WASM_MARKER_BR);
    asm_wasm_emit_uleb128(as, label);
}

void asm_wasm_jump(asm_wasm_t *as, uint label) {
    emit_jump_common(as, label, false);
}

void asm_wasm_jump_if_reg_zero(asm_wasm_t *as, uint reg, uint label) {
    asm_wasm_op_local_get(as, reg);
    asm_wasm_op_i32_const(as, 0);
    asm_wasm_op_binop(as, WASM_OP_I32_EQ);
    emit_jump_common(as, label, true);
}

void asm_wasm_jump_if_reg_nonzero(asm_wasm_t *as, uint reg, uint label) {
    asm_wasm_op_local_get(as, reg);
    emit_jump_common(as, label, true);
}

void asm_wasm_jump_if_reg_eq(asm_wasm_t *as, uint reg1, uint reg2, uint label) {
    asm_wasm_op_local_get(as, reg1);
    asm_wasm_op_local_get(as, reg2);
    asm_wasm_op_binop(as, WASM_OP_I32_EQ);
    emit_jump_common(as, label, true);
}

// ---- Indirect call via mp_fun_table ----

void asm_wasm_call_ind(asm_wasm_t *as, uint fun_table_idx, uint reg_temp) {
    // Call through the imported trampoline (function index 0 in the mini-module).
    // The trampoline calls the host's function table with correct type signatures,
    // avoiding call_indirect type mismatches between varying mp_fun_table entries.
    //
    // Trampoline signature: (table_index, arg1, arg2, arg3, arg4) -> i32
    //
    // Load the host function's table index from mp_fun_table[fun_table_idx]
    asm_wasm_op_local_get(as, ASM_WASM_REG_FUN_TABLE);
    asm_wasm_op_i32_load(as, 2, fun_table_idx * 4);
    // Push the 4 args from locals
    asm_wasm_op_local_get(as, ASM_WASM_REG_ARG_1);
    asm_wasm_op_local_get(as, ASM_WASM_REG_ARG_2);
    asm_wasm_op_local_get(as, ASM_WASM_REG_ARG_3);
    asm_wasm_op_local_get(as, ASM_WASM_REG_ARG_4);
    // Call trampoline (imported function index 0)
    asm_wasm_emit_byte(as, WASM_OP_CALL);
    asm_wasm_emit_uleb128(as, 0); // function index 0 = imported trampoline
    // Store return value
    asm_wasm_op_local_set(as, ASM_WASM_REG_RET);
}

// Direct call to setjmp (legacy fallback when WASM EH is disabled)
#if !MICROPY_WASM_EXCEPTION_HANDLING
void asm_wasm_call_setjmp(asm_wasm_t *as) {
    asm_wasm_op_local_get(as, ASM_WASM_REG_ARG_1);
    asm_wasm_emit_byte(as, WASM_OP_CALL);
    asm_wasm_emit_uleb128(as, 0);
}
#endif

// ---- WASM native exception handling ----
//
// Replaces MicroPython's NLR (setjmp/longjmp) with WASM structured
// exception handling.  The NLR pattern in emitnative.c is:
//
//   nlr_push(&nlr_buf)    →  returns 0 on setup
//   setjmp(nlr_buf.jmpbuf) →  returns 0 on setup, nonzero on exception
//   <body>                 →  may call nlr_jump (= throw)
//   nlr_pop()              →  removes the handler
//
// We translate this to:
//
//   try (result i32)
//     <body>
//     i32.const 0         ;; normal exit: "nlr_push returned 0"
//   catch $nlr_tag
//     local.set $exc_val  ;; exception payload → local (nlr_buf.ret_val)
//     i32.const 1         ;; exception exit: "nlr_push returned nonzero"
//   end
//
// emitnative.c then branches on the result (0 = body, nonzero = handler).
//
// The flow:
//   ASM_CALL_IND(MP_F_NLR_PUSH)  → asm_wasm_nlr_push: emit try + dummy push
//   ASM_CALL_IND(MP_F_SETJMP)    → asm_wasm_nlr_push_tail: nop (absorbed)
//   <body code>
//   ASM_CALL_IND(MP_F_NLR_POP)   → asm_wasm_nlr_pop: emit success path + catch
//
// The try block is left open after nlr_push. emitnative.c emits body code.
// nlr_pop closes the try with the normal-exit value, then the catch clause
// handles exceptions.  The end instruction closes the whole structure.

#if MICROPY_WASM_EXCEPTION_HANDLING

// Low-level opcode emission
void asm_wasm_op_try(asm_wasm_t *as, uint8_t blocktype) {
    asm_wasm_emit_byte(as, WASM_OP_TRY);
    asm_wasm_emit_byte(as, blocktype);
}

void asm_wasm_op_catch(asm_wasm_t *as, uint tag_idx) {
    asm_wasm_emit_byte(as, WASM_OP_CATCH);
    asm_wasm_emit_uleb128(as, tag_idx);
}

void asm_wasm_op_catch_all(asm_wasm_t *as) {
    asm_wasm_emit_byte(as, WASM_OP_CATCH_ALL);
}

void asm_wasm_op_throw(asm_wasm_t *as, uint tag_idx) {
    asm_wasm_emit_byte(as, WASM_OP_THROW);
    asm_wasm_emit_uleb128(as, tag_idx);
}

void asm_wasm_op_rethrow(asm_wasm_t *as, uint depth) {
    asm_wasm_emit_byte(as, WASM_OP_RETHROW);
    asm_wasm_emit_uleb128(as, depth);
}

// High-level NLR replacement functions called from ASM_CALL_IND

void asm_wasm_nlr_push(asm_wasm_t *as) {
    // emitnative.c has already set up REG_ARG_1 = &nlr_buf.
    // We ignore that — WASM EH doesn't need an nlr_buf address.
    // Instead, open a try block that yields i32 (the "nlr_push return value").
    //
    // The try block stays open — emitnative.c will emit the body code,
    // then call nlr_pop (or setjmp) to close it.
    asm_wasm_op_try(as, WASM_BLOCKTYPE_I32);
    as->try_depth++;
}

void asm_wasm_nlr_push_tail(asm_wasm_t *as) {
    // Called for MP_F_SETJMP after nlr_push.
    // In the NLR+setjmp model, setjmp is the actual save point.
    // With WASM EH, the try block from nlr_push is the save point.
    // Nothing to emit — the try is already open.
    //
    // emitnative.c expects REG_RET to hold the "return value" of setjmp.
    // On normal entry, it should be 0.  We don't set it here because
    // the try block's result (pushed at nlr_pop) will be used instead.
    // The branch-if-zero/nonzero after setjmp checks REG_RET, but
    // emitnative.c actually checks the result of nlr_push, which is
    // the try block's overall result.  So this is a nop.
    (void)as;
}

void asm_wasm_nlr_pop(asm_wasm_t *as) {
    // Close the try block's normal path: push 0 (success).
    // Then open the catch clause for the NLR tag.
    //
    // WASM structure at this point:
    //   try (result i32)
    //     <body code emitted by emitnative.c>
    //     i32.const 0              ;; ← we emit this (normal exit)
    //   catch $nlr_tag             ;; ← we emit this
    //     ;; exception payload (i32 — the exception value pointer) is on stack
    //     local.set REG_TEMP1      ;; save exception value (nlr_buf.ret_val)
    //     i32.const 1              ;; exception exit: nonzero
    //   end                        ;; ← we emit this
    //
    // After end, the i32 result is on the stack (0 or 1).
    // emitnative.c stores it to REG_RET and branches.

    // Normal path: success value
    asm_wasm_op_i32_const(as, 0);

    // Catch clause: handle NLR exception
    asm_wasm_op_catch(as, as->nlr_tag_index);

    // The exception payload (an i32 — pointer to the exception object)
    // is pushed onto the stack by the catch instruction.
    // Store it where emitnative.c expects nlr_buf.ret_val:
    // LOCAL_IDX_EXC_VAL is at state offset NLR_BUF_IDX_RET_VAL (= 1).
    asm_wasm_op_local_set(as, ASM_WASM_REG_LOCAL_1 + ASM_WASM_STATE_OFFSET + 1);

    // Exception path: return nonzero
    asm_wasm_op_i32_const(as, 1);

    // Close the try/catch block
    asm_wasm_op_end(as);
    if (as->try_depth > 0) {
        as->try_depth--;
    }

    // Store the result (0 or 1) into REG_RET so emitnative.c can branch on it
    asm_wasm_op_local_set(as, ASM_WASM_REG_RET);
}

#endif // MICROPY_WASM_EXCEPTION_HANDLING

// ---- Cooperative yield ----
//
// At each backward branch (loop header), we emit:
//
//   local.get $budget
//   i32.const 1
//   i32.sub
//   local.tee $budget
//   i32.eqz
//   if
//     i32.const MP_VM_RETURN_YIELD    ;; signal "budget exhausted"
//     return
//   end
//
// The budget local is allocated as an extra WASM local in asm_wasm_entry.
// It's initialized to a default value (e.g., 256 iterations per yield).
// The JS host calls the function again after doing event-loop work.
//
// This is the native-code equivalent of MICROPY_VM_HOOK_LOOP in vm.c.
// For the bytecode interpreter, the hook runs RUN_BACKGROUND_TASKS.
// For native WASM code, we return to JS and let JS run background tasks.

#if MICROPY_WASM_COOPERATIVE_YIELD

// Default budget: number of loop iterations before yielding.
// Higher = more throughput, lower = more responsive.
#ifndef MICROPY_WASM_YIELD_BUDGET
#define MICROPY_WASM_YIELD_BUDGET 256
#endif

// Return value indicating "budget exhausted, call me again".
// Must match MP_VM_RETURN_YIELD if defined, otherwise use a distinct value.
#ifndef MP_VM_RETURN_YIELD
#define MP_VM_RETURN_YIELD 0x7F
#endif

void asm_wasm_emit_yield_check(asm_wasm_t *as) {
    // Decrement budget, check if zero
    asm_wasm_op_local_get(as, as->yield_budget_local);
    asm_wasm_op_i32_const(as, 1);
    asm_wasm_op_binop(as, WASM_OP_I32_SUB);
    asm_wasm_op_local_tee(as, as->yield_budget_local);
    asm_wasm_emit_byte(as, WASM_OP_I32_EQZ);

    // if (budget == 0) return MP_VM_RETURN_YIELD
    asm_wasm_emit_byte(as, WASM_OP_IF);
    asm_wasm_emit_byte(as, WASM_BLOCKTYPE_VOID);
    asm_wasm_op_i32_const(as, MP_VM_RETURN_YIELD);
    asm_wasm_op_local_set(as, ASM_WASM_REG_RET);
    asm_wasm_op_local_get(as, ASM_WASM_REG_RET);
    asm_wasm_op_return(as);
    asm_wasm_op_end(as);
}

#endif // MICROPY_WASM_COOPERATIVE_YIELD

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

void asm_wasm_mov_reg_imm(asm_wasm_t *as, uint reg_dest, int32_t imm) {
    asm_wasm_op_i32_const(as, imm);
    asm_wasm_op_local_set(as, reg_dest);
}

void asm_wasm_mov_reg_local_addr(asm_wasm_t *as, uint reg_dest, uint local_num) {
    asm_wasm_op_i32_const(as, 0); // placeholder — WASM locals aren't addressable
    asm_wasm_op_local_set(as, reg_dest);
}

void asm_wasm_mov_reg_pcrel(asm_wasm_t *as, uint reg_dest, uint label) {
    // On ARM/x86, this loads the native address of `label` into a register.
    // On WASM, we store a STATE INDEX that br_table will dispatch on.
    // The index is assigned by register_yield_label during code generation.
    //
    // During COMPUTE pass: register the label, get its index.
    // During EMIT pass: emit i32.const <index>.
    uint idx = asm_wasm_register_yield_label(as, label);
    asm_wasm_op_i32_const(as, (int32_t)idx);
    asm_wasm_op_local_set(as, reg_dest);
}

uint asm_wasm_register_yield_label(asm_wasm_t *as, uint label) {
    // Check if this label is already registered
    for (uint i = 0; i < as->num_yield_labels; i++) {
        if (as->yield_labels[i] == label) {
            return i + 1;  // +1 because index 0 = first entry (start of body)
        }
    }
    // Register new yield label
    if (as->num_yield_labels < ASM_WASM_MAX_YIELD_LABELS) {
        as->yield_labels[as->num_yield_labels] = label;
        as->num_yield_labels++;
    }
    return as->num_yield_labels;  // 1-based index (0 = initial entry)
}

void asm_wasm_jump_reg(asm_wasm_t *as, uint reg) {
    // Generator re-entry dispatch via br_table.
    //
    // The state index in `reg` was saved by asm_wasm_mov_reg_pcrel.
    // Index 0 = first entry (fall through to generator body start).
    // Index N = re-entry after the Nth yield point.
    //
    // We emit:
    //   block $L0          ;; target for index 0 (initial entry)
    //     block $L1        ;; target for index 1 (first yield re-entry)
    //       block $L2      ;; target for index 2
    //         ...
    //         local.get <reg>
    //         br_table 0 1 2 ... N  ;; default = 0 (initial entry)
    //       end             ;; $LN lands here → jump to label N
    //       <marker: jump to yield_labels[N-1]>
    //     end               ;; $L1 lands here
    //     <marker: jump to yield_labels[0]>
    //   end                 ;; $L0 lands here (fall through = initial entry)
    //
    // IMPORTANT: br_table targets are relative to block nesting.
    // br 0 = innermost block, br N = Nth enclosing block.
    // The block nesting is: $L0 outermost, $L(N) innermost.
    // So br_table entry for state S should branch to depth (num_yields - S).

    uint n = as->num_yield_labels;

    if (n == 0) {
        // No yield points — this shouldn't happen in a generator, but
        // emit a simple return as fallback.
        asm_wasm_op_local_get(as, reg);
        asm_wasm_op_return(as);
        return;
    }

    // Open N+1 blocks (one for initial entry + one per yield point).
    // All are void blocks since we just branch for control flow.
    for (uint i = 0; i <= n; i++) {
        asm_wasm_op_block(as, WASM_BLOCKTYPE_VOID);
    }

    // Load state index and emit br_table
    asm_wasm_op_local_get(as, reg);
    asm_wasm_emit_byte(as, WASM_OP_BR_TABLE);
    asm_wasm_emit_uleb128(as, n);  // number of targets (not counting default)

    // br_table targets: state 0 → depth N (outermost = initial entry),
    // state 1 → depth N-1, ..., state N → depth 0 (innermost).
    for (uint s = 0; s <= n; s++) {
        uint depth = n - s;
        asm_wasm_emit_uleb128(as, depth);
    }

    // Close blocks from innermost to outermost, emitting jumps to
    // the corresponding yield labels.
    // Block $L(N) (innermost) → yield_labels[N-1] (last yield re-entry)
    // Block $L(N-1) → yield_labels[N-2]
    // ...
    // Block $L(1) → yield_labels[0] (first yield re-entry)
    // Block $L(0) → fall through (initial entry, no jump needed)
    for (uint i = n; i >= 1; i--) {
        asm_wasm_op_end(as);  // close block $L(i)
        // Jump to the yield re-entry label.
        // Use our marker-based jump which the JS rewriter handles.
        emit_jump_common(as, as->yield_labels[i - 1], false);
    }
    asm_wasm_op_end(as);  // close block $L(0) — initial entry falls through
}

// ---- Assembler lifecycle ----

void asm_wasm_init(asm_wasm_t *as, size_t max_num_labels) {
    memset(as, 0, sizeof(*as));
    mp_asm_base_init(&as->base, max_num_labels);
    as->label_is_loop = m_new0(uint8_t, max_num_labels);
    as->label_is_forward = m_new0(uint8_t, max_num_labels);
}

void asm_wasm_free(asm_wasm_t *as, bool free_code) {
    if (as->label_is_loop != NULL) {
        m_del(uint8_t, as->label_is_loop, as->base.max_num_labels);
        as->label_is_loop = NULL;
    }
    if (as->label_is_forward != NULL) {
        m_del(uint8_t, as->label_is_forward, as->base.max_num_labels);
        as->label_is_forward = NULL;
    }
    mp_asm_base_deinit(&as->base, free_code);
}

void asm_wasm_start_pass(asm_wasm_t *as, uint pass) {
    mp_asm_base_start_pass(&as->base, pass);
    as->block_depth = 0;
    #if MICROPY_WASM_EXCEPTION_HANDLING
    as->try_depth = 0;
    // Tag index 0 is the NLR tag (declared in the module's tag section
    // or imported from the host).  The JS module builder assigns this.
    as->nlr_tag_index = 0;
    #endif
    as->num_yield_labels = 0;
    if (pass == MP_ASM_PASS_COMPUTE) {
        if (as->label_is_loop != NULL) {
            memset(as->label_is_loop, 0, as->base.max_num_labels);
        }
        if (as->label_is_forward != NULL) {
            memset(as->label_is_forward, 0, as->base.max_num_labels);
        }
    }
}

bool asm_wasm_end_pass(asm_wasm_t *as) {
    // The JS rewriter adds block/loop/end structure and the final end.
    // We just emit the trailing end for the function body.
    asm_wasm_op_end(as);
    return false;
}

void asm_wasm_entry(asm_wasm_t *as, int num_locals) {
    // Lazy-init WASM-specific fields (EXPORT_FUN(new) only calls mp_asm_base_init)
    if (as->label_is_loop == NULL && as->base.max_num_labels > 0) {
        as->label_is_loop = m_new0(uint8_t, as->base.max_num_labels);
        as->label_is_forward = m_new0(uint8_t, as->base.max_num_labels);
    }
    // Clear label analysis at start of each COMPUTE pass
    if (as->base.pass == MP_ASM_PASS_COMPUTE) {
        if (as->label_is_loop != NULL) {
            memset(as->label_is_loop, 0, as->base.max_num_labels);
        }
        if (as->label_is_forward != NULL) {
            memset(as->label_is_forward, 0, as->base.max_num_labels);
        }
    }
    as->block_depth = 0;

    // Calculate total locals needed.
    uint base_locals = ASM_WASM_NUM_EXTRA_LOCALS + ASM_WASM_STATE_OFFSET + num_locals;
    #if MICROPY_WASM_COOPERATIVE_YIELD
    // Reserve one extra local for the yield budget counter.
    // It sits after all other locals.
    as->yield_budget_local = ASM_WASM_NUM_REGS + ASM_WASM_STATE_OFFSET + num_locals;
    base_locals += 1;
    #endif
    as->num_locals = ASM_WASM_NUM_REGS + ASM_WASM_STATE_OFFSET + num_locals
        #if MICROPY_WASM_COOPERATIVE_YIELD
        + 1
        #endif
        ;

    // Emit local declarations.
    // ASM_WASM_STATE_OFFSET accounts for the gap between register locals
    // (REG_LOCAL_1..3) and the emitter's state stack. On WASM, both live
    // in the same local index space, so we need extra locals to avoid overlap.
    if (base_locals > 0) {
        asm_wasm_emit_uleb128(as, 1);
        asm_wasm_emit_uleb128(as, base_locals);
        asm_wasm_emit_byte(as, WASM_TYPE_I32);
    } else {
        asm_wasm_emit_uleb128(as, 0);
    }

    as->func_body_offset = as->base.code_offset;

    #if MICROPY_WASM_COOPERATIVE_YIELD
    // Initialize the budget counter to the default value.
    asm_wasm_op_i32_const(as, MICROPY_WASM_YIELD_BUDGET);
    asm_wasm_op_local_set(as, as->yield_budget_local);
    #endif

    // During EMIT pass, open blocks for all forward-target labels.
    // They are opened in reverse label order so that the first forward
    // label is the innermost block (lowest br depth).
    // Block structure is handled by the JS rewriter in library_asmwasm.js.
    // The C emitter produces flat code with special marker opcodes that the
    // rewriter transforms into proper block/loop/end nesting.
}

void asm_wasm_exit(asm_wasm_t *as) {
    // Push return value and return explicitly
    asm_wasm_op_local_get(as, ASM_WASM_REG_RET);
    asm_wasm_op_return(as);
}

#endif // MICROPY_EMIT_WASM
