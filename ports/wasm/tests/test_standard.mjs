// test_standard.mjs — Smoke test for wasm-tmp standard variant.
//
// Tests that the WASM binary loads, exports are present, and the
// frame loop initializes without crashing.

import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';

const wasmBytes = await readFile(new URL('../build-standard/circuitpython.wasm', import.meta.url));
const wasm = await WebAssembly.compile(wasmBytes);

// WASI provides fd operations for our VFS POSIX layer
const wasi = new WASI({
    version: 'preview1',
    preopens: { '/': '.' },
});

// Build import object: WASI + our port imports (stubs for now)
const imports = {
    ...wasi.getImportObject(),
    ffi: {
        request_frame: () => {},
        notify: (type, pin, arg, data) => {},
    },
    port: {
        getCpuTemperature: () => 25000,
        getCpuVoltage: () => 3300,
        getMonotonicMs: () => Math.floor(performance.now()),
        registerPinListener: (pin) => {},
        unregisterPinListener: (pin) => {},
    },
    memfs: {
        register: (pathPtr, pathLen, dataPtr, dataSize) => {},
    },
};

let instance;
try {
    instance = await WebAssembly.instantiate(wasm, imports);
} catch (e) {
    console.error('FAIL: instantiation error:', e.message);
    process.exit(1);
}

const exports = instance.exports;

// Check critical exports exist
const requiredExports = [
    'chassis_init', 'chassis_frame',
    'cp_port_memory_addr', 'cp_port_memory_size',
    'cp_ctrl_c', 'cp_ctrl_d',
    'cp_input_buf_addr', 'cp_input_buf_size',
    'cp_start_code', 'cp_start_repl',
    'hal_mark_gpio_dirty', 'hal_mark_analog_dirty',
    'hal_get_change_count',
];

let missing = [];
for (const name of requiredExports) {
    if (typeof exports[name] !== 'function') {
        missing.push(name);
    }
}

if (missing.length > 0) {
    console.error('FAIL: missing exports:', missing.join(', '));
    process.exit(1);
}
console.log('OK: all required exports present');

// Initialize
try {
    wasi.initialize(instance);
} catch (e) {
    // Some WASI implementations want start() not initialize()
    // If neither works, the binary may define _start
}

try {
    exports.chassis_init();
    console.log('OK: chassis_init succeeded');
} catch (e) {
    console.error('FAIL: chassis_init error:', e.message);
    process.exit(1);
}

// Check port memory
const memAddr = exports.cp_port_memory_addr();
const memSize = exports.cp_port_memory_size();
console.log(`OK: port_mem at 0x${memAddr.toString(16)}, ${memSize} bytes`);

if (memSize < 512 * 1024) {
    console.error(`FAIL: port_mem too small: ${memSize} (expected >= 512K for GC heap)`);
    process.exit(1);
}

// Run a frame
try {
    const now_us = Math.floor(performance.now() * 1000);
    const rc = exports.chassis_frame(now_us, 10000); // 10ms budget
    console.log(`OK: chassis_frame returned RC=${rc}`);
} catch (e) {
    console.error('FAIL: chassis_frame error:', e.message);
    process.exit(1);
}

// Test HAL
try {
    exports.hal_mark_gpio_dirty(5);
    const changes = exports.hal_get_change_count();
    console.log(`OK: hal_mark_gpio_dirty(5), change_count=${changes}`);
} catch (e) {
    console.error('FAIL: HAL error:', e.message);
    process.exit(1);
}

// Run a few more frames
for (let i = 0; i < 5; i++) {
    const now_us = Math.floor(performance.now() * 1000);
    exports.chassis_frame(now_us, 10000);
}
console.log('OK: 5 additional frames completed');

console.log('\nAll tests passed.');
