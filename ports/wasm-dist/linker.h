/*
 * linker.h — WASI port linker macros
 *
 * ITCM/DTCM concepts from ARM Cortex adapted for WASI:
 *   ITCM = code stays in WASM (VM dispatch, GC, compiler) — no-op
 *   DTCM = data that could be checkpointed to OPFS — no-op for now
 */
#pragma once

#define PLACE_IN_ITCM(name)         name
#define PLACE_IN_DTCM_DATA(name)    name
#define PLACE_IN_DTCM_BSS(name)     name
