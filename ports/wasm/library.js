/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2017, 2018 Rami Ali
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

mergeInto(LibraryManager.library, {
    // Get pointer to virtual hardware shared memory
    mp_js_ticks_ms__postset: `
        var virtualHardwarePtr = null;
        var cachedHeapBuffer = null;

        // Initialize virtual hardware access
        function initVirtualHardware() {
            if (virtualHardwarePtr === null) {
                try {
                    virtualHardwarePtr = Module.ccall('get_virtual_hardware_ptr', 'number', [], []);
                } catch (e) {
                    console.warn('Virtual hardware not available, falling back to Date.now():', e);
                }
            }
        }

        // Get a fresh DataView (handles WASM memory growth)
        function getVirtualHardwareView() {
            if (virtualHardwarePtr === null) {
                return null;
            }
            // Check if heap buffer changed (memory grew)
            if (cachedHeapBuffer !== Module.HEAPU8.buffer) {
                cachedHeapBuffer = Module.HEAPU8.buffer;
            }
            // Always create fresh DataView from current buffer
            // Structure size: uint64_t (8) + uint32_t (4) + uint8_t (1) + padding (3) = 16 bytes
            return new DataView(cachedHeapBuffer, virtualHardwarePtr, 16);
        }

        var MP_JS_EPOCH = Date.now();
    `,

    mp_js_ticks_ms: () => {
        // Lazy initialization
        if (virtualHardwarePtr === null) {
            initVirtualHardware();
        }

        // If virtual hardware is available, read from it
        if (virtualHardwarePtr !== null) {
            try {
                const view = getVirtualHardwareView();
                if (view !== null) {
                    // Read uint64_t ticks_32khz (8 bytes, little-endian)
                    const ticks32kHzLow = view.getUint32(0, true);
                    const ticks32kHzHigh = view.getUint32(4, true);
                    const ticks32kHz = (BigInt(ticks32kHzHigh) << 32n) | BigInt(ticks32kHzLow);

                    // Convert 32kHz ticks to milliseconds (ticks / 32)
                    const milliseconds = Number(ticks32kHz / 32n);
                    return milliseconds;
                }
            } catch (e) {
                console.warn('Error reading virtual hardware, falling back to Date.now():', e);
            }
        }

        // Fallback to real time if virtual hardware not available
        return Date.now() - MP_JS_EPOCH;
    },

    mp_js_hook: () => {
        if (ENVIRONMENT_IS_NODE) {
            const mp_interrupt_char = Module.ccall(
                "mp_hal_get_interrupt_char",
                "number",
                ["number"],
                ["null"],
            );
            const fs = require("fs");

            const buf = Buffer.alloc(1);
            try {
                const n = fs.readSync(process.stdin.fd, buf, 0, 1);
                if (n > 0) {
                    if (buf[0] === mp_interrupt_char) {
                        Module.ccall(
                            "mp_sched_keyboard_interrupt",
                            "null",
                            ["null"],
                            ["null"],
                        );
                    } else {
                        process.stdout.write(String.fromCharCode(buf[0]));
                    }
                }
            } catch (e) {
                if (e.code === "EAGAIN") {
                } else {
                    throw e;
                }
            }
        }
    },

    mp_js_time_ms: () => {
        // Use virtual hardware if available, otherwise fall back to Date.now()
        if (virtualHardwarePtr === null) {
            initVirtualHardware();
        }

        if (virtualHardwarePtr !== null) {
            try {
                const view = getVirtualHardwareView();
                if (view !== null) {
                    // Read uint64_t ticks_32khz (8 bytes, little-endian)
                    const ticks32kHzLow = view.getUint32(0, true);
                    const ticks32kHzHigh = view.getUint32(4, true);
                    const ticks32kHz = (BigInt(ticks32kHzHigh) << 32n) | BigInt(ticks32kHzLow);

                    // Convert to milliseconds and add epoch for absolute time
                    const milliseconds = Number(ticks32kHz / 32n);
                    return MP_JS_EPOCH + milliseconds;
                }
            } catch (e) {
                console.warn('Error reading virtual hardware for time_ms, falling back:', e);
            }
        }

        return Date.now();
    },

    // Node prior to v19 did not expose "crypto" as a global, so make sure it exists.
    mp_js_random_u32__postset:
        "if (globalThis.crypto === undefined) { globalThis.crypto = require('crypto'); }",

    mp_js_random_u32: () =>
        globalThis.crypto.getRandomValues(new Uint32Array(1))[0],
});
