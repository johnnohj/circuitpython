/*
 * linker.h — WASM port linker macros
 *
 * On ARM Cortex-M, ITCM/DTCM are physically separate fast memory regions.
 * On WASM, all memory is linear — these macros are identity no-ops.
 *
 * ITCM: WASM JIT handles code placement.  Hot functions (GC marking,
 *       pystack alloc, background callbacks) are already optimal.
 * DTCM: Static arrays provide stable addresses in WASM linear memory.
 *       Hardware state uses /hal/ WASI fd paths (wasi-memfs.js).
 *       modmachine.c provides machine.mem8/16/32 for Python access.
 */
#pragma once

#define PLACE_IN_ITCM(name)         name
#define PLACE_IN_DTCM_DATA(name)    name
#define PLACE_IN_DTCM_BSS(name)     name
