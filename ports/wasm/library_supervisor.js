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
        var virtualClockPtr = null;
        var cachedHeapBuffer = null;

        function initVirtualClock() {
            if (virtualClockPtr === null) {
                try {
                    virtualClockPtr = Module.ccall('get_virtual_clock_hw_ptr', 'number', [], []);
                } catch (e) {
                    console.warn('Virtual clock not available, falling back to Date.now():', e);
                }
            }
        }

        function getVirtualClockView() {
            if (virtualClockPtr === null) return null;
            if (cachedHeapBuffer !== Module.HEAPU8.buffer) {
                cachedHeapBuffer = Module.HEAPU8.buffer;
            }
            // virtual_clock_hw_t: uint64 ticks_32khz + uint32 cpu_freq + uint8 mode + uint64 yields + uint64 js_ticks
            return new DataView(cachedHeapBuffer, virtualClockPtr, 32);
        }

        var MP_JS_EPOCH = Date.now();
    `,

    mp_js_ticks_ms: () => {
        if (virtualClockPtr === null) initVirtualClock();

        if (virtualClockPtr !== null) {
            try {
                const view = getVirtualClockView();
                if (view !== null) {
                    const ticks32kHzLow = view.getUint32(0, true);
                    const ticks32kHzHigh = view.getUint32(4, true);
                    const ticks32kHz = (BigInt(ticks32kHzHigh) << 32n) | BigInt(ticks32kHzLow);
                    return Number(ticks32kHz / 32n);  // Convert 32kHz to milliseconds
                }
            } catch (e) {
                console.warn('Error reading virtual clock:', e);
            }
        }

        return Date.now() - MP_JS_EPOCH;
    },

    mp_js_time_ms: () => {
        if (virtualClockPtr === null) initVirtualClock();

        if (virtualClockPtr !== null) {
            try {
                const view = getVirtualClockView();
                if (view !== null) {
                    const ticks32kHzLow = view.getUint32(0, true);
                    const ticks32kHzHigh = view.getUint32(4, true);
                    const ticks32kHz = (BigInt(ticks32kHzHigh) << 32n) | BigInt(ticks32kHzLow);
                    return MP_JS_EPOCH + Number(ticks32kHz / 32n);
                }
            } catch (e) {
                console.warn('Error reading virtual clock for time_ms:', e);
            }
        }

        return Date.now();
    },

    // ==========================================================================
    // Node.js Hook (from MicroPython)
    // ==========================================================================

    mp_js_hook: () => {
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
    },

    // ==========================================================================
    // Cryptographic Random (from MicroPython)
    // ==========================================================================

    mp_js_random_u32__postset:
        "if (globalThis.crypto === undefined) { globalThis.crypto = require('crypto'); }",

    mp_js_random_u32: () =>
        globalThis.crypto.getRandomValues(new Uint32Array(1))[0],
});
