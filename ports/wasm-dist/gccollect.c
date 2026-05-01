// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2013, 2014 Damien P. George
// SPDX-FileCopyrightText: Based on ports/wasm/gccollect.c by CircuitPython contributors
// SPDX-FileCopyrightText: Adapted by CircuitPython WASM Port Devs
//
// SPDX-License-Identifier: MIT
//
// gccollect.c — Garbage collection root scanning.
//
// On WASM, the GC heap lives in port_mem (MEMFS-in-linear-memory).
// We scan registers + C stack via gc_helper, then scan all non-free
// pystack regions as additional GC roots.
//
// Design refs:
//   design/wasm-layer.md                    (wasm layer, port owns memory)
//   design/behavior/05-vm-lifecycle.md      (GC heap, pystack)

#include <stdio.h>

#include "py/mpstate.h"
#include "py/gc.h"

#include "shared/runtime/gchelper.h"

#if MICROPY_ENABLE_GC

void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();

    #if MICROPY_PY_THREAD
    mp_thread_gc_others();
    #endif

    // TODO(Phase 4.4): Scan all context pystack regions as GC roots.
    // Inactive contexts' pystacks are in static arrays in port_mem,
    // so the GC can see them directly — no copy needed.
    //
    // When supervisor/context.c migrates:
    //   #include "supervisor/context.h"
    //   cp_context_gc_collect();

    gc_collect_end();
}

#endif // MICROPY_ENABLE_GC
