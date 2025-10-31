// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython contributors
//
// SPDX-License-Identifier: MIT

// ==============================================================================
// Supervisor Functions - Timing and System Control
// ==============================================================================
// Virtual clock hardware for CircuitPython supervisor timing system
// State managed in supervisor/port.c

mergeInto(LibraryManager.library, {
    // ==========================================================================
    // Virtual Clock (from supervisor/port.c)
    // ==========================================================================

    mp_js_ticks_ms__postset: `
        var MP_JS_EPOCH = Date.now();
    `,

    mp_js_ticks_ms: () => {
        return Date.now() - MP_JS_EPOCH;
    },

    mp_js_time_ms: () => {
        return Date.now();
    },

    // ==========================================================================
    // VM Hook - Cooperative Yielding and Node.js Input
    // ==========================================================================
    // Called periodically by MICROPY_VM_HOOK_LOOP (every 10 bytecodes)
    //
    // ASYNCIFIED VARIANT: Uses C implementation in supervisor/port.c
    //   The C version calls emscripten_sleep(0) for proper ASYNCIFY yielding
    //
    // OTHER VARIANTS: Uses this JavaScript implementation
    //   Just checks Node.js stdin for interrupt characters
    //
    // NOTE: If a C function with the same name exists, it takes precedence.
    // The asyncified variant defines mp_js_hook() in C, so this JS version
    // becomes a fallback for other variants.

    mp_js_hook__postset: `
        // Track last yield time for cooperative yielding
        var MP_JS_LAST_YIELD_TIME = Date.now();
        var MP_JS_YIELD_INTERVAL_MS = 16; // ~60fps
    `,

    mp_js_hook: () => {
        // Asyncified variant: Delegate to C implementation for ASYNCIFY yielding
        // Use ccall to properly handle ASYNCIFY context (not direct function call)
        if (typeof Module._mp_js_hook_asyncify_impl !== 'undefined') {
            try {
                // ccall properly manages ASYNCIFY unwinding/rewinding
                Module.ccall('mp_js_hook_asyncify_impl', null, [], []);
                return;
            } catch (e) {
                // C implementation failed, fall through to JS version
            }
        }

        // Node.js stdin checking (original functionality from MicroPython)
        if (ENVIRONMENT_IS_NODE) {
            const mp_interrupt_char = Module.ccall("mp_hal_get_interrupt_char", "number", ["number"], ["null"]);
            const fs = require("fs");
            const buf = Buffer.alloc(1);
            try {
                const n = fs.readSync(process.stdin.fd, buf, 0, 1);
                if (n > 0) {
                    if (buf[0] === mp_interrupt_char) {
                        Module.ccall("mp_sched_keyboard_interrupt", "null", ["null"], ["null"]);
                    } else {
                        process.stdout.write(String.fromCharCode(buf[0]));
                    }
                }
            } catch (e) {
                if (e.code !== "EAGAIN") {
                    throw e;
                }
            }
        }

        // For non-asyncified variants: No cooperative yielding implemented
        // The integrated variant uses code_analysis.c for exception-based yielding
        // The standard variant has no yielding at all
    },

    // ==========================================================================
    // Cryptographic Random (from MicroPython)
    // ==========================================================================

    mp_js_random_u32__postset:
        "if (globalThis.crypto === undefined) { globalThis.crypto = require('crypto'); }",

    mp_js_random_u32: () =>
        globalThis.crypto.getRandomValues(new Uint32Array(1))[0],
});
