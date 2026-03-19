/*
 * WASM-dist linker.h — ITCM/DTCM macro adaptation for Emscripten
 *
 * On ARM Cortex targets these macros place code/data in fast tightly-coupled
 * memory (ITCM = instruction, DTCM = data).  For the WASM-dist port we adapt
 * the semantics:
 *
 *   ITCM  — performance-critical code path (VM dispatch, GC mark, pystack
 *            alloc).  Maps to __attribute__((noinline)) so JS engines can
 *            tier-compile each hot function independently.
 *
 *   DTCM  — frequently-accessed VM state (mp_state_ctx, bytecode entry table).
 *            These variables are part of the data that is checkpointed to
 *            Emscripten MEMFS at /mem/ on suspend.  The macro expands to a
 *            no-op because the data lives in normal WASM linear memory; the
 *            label is preserved as documentation of checkpoint scope.
 */
#pragma once

/* ITCM: keep noinline — creates JIT-friendly function boundaries */
#define PLACE_IN_ITCM(name)        __attribute__((noinline)) name

/* DTCM: no-op for WASM; semantic: this data is checkpointed to /mem/ */
#define PLACE_IN_DTCM_DATA(name)   name
#define PLACE_IN_DTCM_BSS(name)    name
