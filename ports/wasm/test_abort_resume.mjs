#!/usr/bin/env node
/**
 * test_abort_resume.mjs — Run the abort-resume WASI test binary.
 *
 * Usage: node test_abort_resume.mjs [path/to/test_abort_resume.wasm]
 */

import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';

const wasmPath = process.argv[2] || 'build-standard/test_abort_resume.wasm';

const wasi = new WASI({
    version: 'preview1',
    args: ['test_abort_resume'],
    env: {},
});

const wasmBytes = await readFile(wasmPath);

// Stub all env imports — these are CircuitPython supervisor functions
// that the test binary links against but doesn't actually call.
let wasmMemory = null;
const envStubs = new Proxy({}, {
    get: (_target, name) => {
        // mp_hal_stdout_tx_strn_cooked: write string to stdout
        if (name === 'mp_hal_stdout_tx_strn_cooked') {
            return (ptr, len) => {
                const bytes = new Uint8Array(wasmMemory.buffer, ptr, len);
                process.stdout.write(bytes);
                return len;
            };
        }
        // mp_hal_stdout_tx_str: write C string to stdout
        if (name === 'mp_hal_stdout_tx_str') {
            return (ptr) => {
                const bytes = new Uint8Array(wasmMemory.buffer, ptr);
                const end = bytes.indexOf(0);
                process.stdout.write(bytes.subarray(0, end));
            };
        }
        return (..._args) => {
            if (name === 'mp_hal_is_interrupted') return 0;
            if (name === 'mp_hal_ticks_ms') return Date.now() & 0x7FFFFFFF;
            if (name === 'supervisor_ticks_ms') return Date.now() & 0x7FFFFFFF;
            if (name === 'stack_ok') return 1;
            if (name === 'assert_heap_ok') return;
            return 0;
        };
    },
});

const { instance } = await WebAssembly.instantiate(wasmBytes, {
    wasi_snapshot_preview1: wasi.wasiImport,
    env: envStubs,
    memfs: {
        register: () => {},
    },
    ffi: {
        request_frame: () => {},
        notify: () => {},
    },
});

wasmMemory = instance.exports.memory;

try {
    wasi.start(instance);
} catch (e) {
    if (e.code === 'ERR_WASI_EXIT' || e.message?.includes('exit')) {
        // WASI exit — check code
        const code = e.exitCode ?? 0;
        if (code !== 0) {
            console.error(`Test exited with code ${code}`);
            process.exit(code);
        }
    } else {
        throw e;
    }
}
