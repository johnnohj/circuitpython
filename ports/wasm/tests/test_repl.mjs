// test_repl.mjs — Test the event-driven REPL via port_mem serial rings.
//
// Feeds characters into the serial_rx ring (via cp_serial_push),
// runs frames, and reads output from the serial_tx ring.
// This validates the full path: JS → rx ring → upstream serial
// multiplexer → REPL → tx ring → JS.

import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';

const wasmBytes = await readFile(new URL('../build-standard/circuitpython.wasm', import.meta.url));
const wasm = await WebAssembly.compile(wasmBytes);

const wasi = new WASI({ version: 'preview1', preopens: { '/': '.' } });

let frameRequested = false;
const imports = {
    ...wasi.getImportObject(),
    ffi: {
        request_frame: () => { frameRequested = true; },
        notify: () => {},
    },
    port: {
        getCpuTemperature: () => 25000,
        getCpuVoltage: () => 3300,
        getMonotonicMs: () => Math.floor(performance.now()),
        registerPinListener: () => {},
        unregisterPinListener: () => {},
    },
    memfs: {
        register: () => {},
    },
};

const instance = await WebAssembly.instantiate(wasm, imports);
const ex = instance.exports;

try { wasi.initialize(instance); } catch {}

// ── Serial TX ring reader ──
// Use exported address — no hardcoded layout math.
const serialTxAddr = ex.cp_serial_tx_addr();
const RING_DATA_OFFSET = 8;  // write_head(u32) + read_head(u32)
const RING_DATA_SIZE = 4096 - 8;

function drainSerialTx() {
    const buf = ex.memory.buffer;
    const view = new DataView(buf);
    const ringBase = serialTxAddr;
    let readHead = view.getUint32(ringBase + 4, true);
    const writeHead = view.getUint32(ringBase, true);

    let output = '';
    const bytes = new Uint8Array(buf);
    while (readHead !== writeHead) {
        output += String.fromCharCode(bytes[ringBase + RING_DATA_OFFSET + readHead]);
        readHead = (readHead + 1) % RING_DATA_SIZE;
    }
    // Advance read head
    view.setUint32(ringBase + 4, readHead, true);
    return output;
}

function writeSerialRx(text) {
    for (let i = 0; i < text.length; i++) {
        ex.cp_serial_push(text.charCodeAt(i));
    }
}

function runFrames(n, budgetUs = 50000) {
    for (let i = 0; i < n; i++) {
        const now = Math.floor(performance.now() * 1000);
        ex.chassis_frame(now, budgetUs);
    }
}

// ── Init ──
ex.chassis_init();
runFrames(1);  // triggers mp_init via step_init
drainSerialTx();  // discard any init output

// ── Start REPL ──
console.log('Test: REPL lifecycle');
ex.cp_start_repl();
runFrames(3);

let output = drainSerialTx();
console.log('  Banner:', JSON.stringify(output.slice(0, 80)));
const hasBanner = output.includes('CircuitPython') || output.includes('>>>');
console.log('  Has banner/prompt:', hasBanner);

// ── Send "1+1\r" ──
writeSerialRx('1+1\r');
runFrames(5);

output = drainSerialTx();
console.log('  After "1+1":', JSON.stringify(output.slice(0, 80)));
const hasResult = output.includes('2');
console.log('  Contains "2":', hasResult);

// ── Send "print('hello')\r" ──
writeSerialRx("print('hello')\r");
runFrames(5);

output = drainSerialTx();
console.log('  After print:', JSON.stringify(output.slice(0, 80)));
const hasHello = output.includes('hello');
console.log('  Contains "hello":', hasHello);

// ── Summary ──
if (hasBanner && hasResult && hasHello) {
    console.log('\nREPL test PASSED');
} else {
    console.log('\nREPL test FAILED');
    if (!hasBanner) console.log('  - No banner/prompt');
    if (!hasResult) console.log('  - "1+1" did not produce "2"');
    if (!hasHello) console.log('  - print("hello") did not produce output');
    process.exit(1);
}
