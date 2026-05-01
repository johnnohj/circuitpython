// test_sync.mjs — validate the sync bus in Node.js
//
// Run: node test_sync.mjs
// Requires: sync.wasm built (make)

import { SyncBus } from './sync.mjs';

// Node.js fetch polyfill for local file
import { readFile } from 'node:fs/promises';
const originalFetch = globalThis.fetch;
globalThis.fetch = async (url) => {
    if (typeof url === 'string' && url.startsWith('file://')) {
        url = new URL(url).pathname;
    }
    if (typeof url === 'string' && !url.startsWith('http')) {
        const data = await readFile(url);
        return { arrayBuffer: () => data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength) };
    }
    return originalFetch(url);
};

let passed = 0;
let failed = 0;

function assert(cond, msg) {
    if (cond) { passed++; }
    else { failed++; console.error(`  FAIL: ${msg}`); }
}

async function run() {
    const bus = await SyncBus.create({
        wasmUrl: new URL('./sync.wasm', import.meta.url).pathname,
        shared: false,  // Node.js without flags may not support SAB
    });

    console.log('Test: Region creation');
    const gpioOff = bus.region('gpio', { size: 384, type: 'slots', slotSize: 12 });
    const txOff = bus.region('serial_tx', { size: 4096, type: 'ring' });
    const fbOff = bus.region('framebuffer', { size: 1024, type: 'slab' });
    assert(gpioOff > 0, 'gpio offset > 0');
    assert(txOff > 0, 'serial_tx offset > 0');
    assert(fbOff > 0, 'framebuffer offset > 0');
    assert(gpioOff !== txOff && txOff !== fbOff, 'offsets are unique');

    const port = bus.port();

    // ── Slab tests ──
    console.log('Test: Slab read/write');
    assert(!port.changed('framebuffer'), 'slab not dirty initially');
    port.writeSlab('framebuffer', new Uint8Array([1, 2, 3, 4]));
    assert(port.changed('framebuffer'), 'slab dirty after write');
    const fb = port.read('framebuffer');
    assert(fb[0] === 1 && fb[1] === 2, 'slab data correct');
    assert(!port.changed('framebuffer'), 'slab clean after read');

    // ── Ring tests ──
    console.log('Test: Ring push/drain');
    const hello = new TextEncoder().encode('Hello');
    const written = port.push('serial_tx', hello);
    assert(written === 5, `pushed 5 bytes (got ${written})`);
    assert(port.available('serial_tx') === 5, 'ring has 5 bytes available');

    const drained = port.drain('serial_tx');
    assert(drained.length === 5, `drained 5 bytes (got ${drained.length})`);
    assert(new TextDecoder().decode(drained) === 'Hello', 'ring data matches');
    assert(port.available('serial_tx') === 0, 'ring empty after drain');

    // Ring wrap-around
    console.log('Test: Ring wrap-around');
    const big = new Uint8Array(4000);
    big.fill(0xAA);
    port.push('serial_tx', big);
    assert(port.available('serial_tx') === 4000, 'pushed 4000 bytes');
    port.drain('serial_tx');
    // Push again after drain — tests wrap
    const wrap = new Uint8Array(100);
    wrap.fill(0xBB);
    port.push('serial_tx', wrap);
    const wrapDrained = port.drain('serial_tx');
    assert(wrapDrained.length === 100, 'wrap-around drain correct');
    assert(wrapDrained[0] === 0xBB, 'wrap-around data correct');

    // ── Slot tests ──
    console.log('Test: Slot read/write');
    const pinData = new Uint8Array([1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0]);
    port.writeSlot('gpio', 5, pinData);
    const readBack = port.readSlot('gpio', 5);
    assert(readBack[0] === 1 && readBack[2] === 1, 'slot data correct');

    const dirty = port.dirtySlots('gpio');
    assert(dirty.length === 1 && dirty[0] === 5, `dirty slot is 5 (got ${dirty})`);
    const dirty2 = port.dirtySlots('gpio');
    assert(dirty2.length === 0, 'dirty cleared after drain');

    // Multiple dirty slots
    port.writeSlot('gpio', 0, new Uint8Array(12));
    port.writeSlot('gpio', 3, new Uint8Array(12));
    port.writeSlot('gpio', 7, new Uint8Array(12));
    const multiDirty = port.dirtySlots('gpio');
    assert(multiDirty.length === 3, `3 dirty slots (got ${multiDirty.length})`);
    assert(multiDirty.includes(0) && multiDirty.includes(3) && multiDirty.includes(7),
        'correct dirty slots');

    // ── Transfer test ──
    console.log('Test: Bus transfer');
    const desc = bus.transfer();
    const remoteBus = SyncBus.fromTransfer(desc);
    const remotePort = remoteBus.port();

    // Write on original, read on remote
    port.push('serial_tx', new TextEncoder().encode('XYZ'));
    const remoteDrained = remotePort.drain('serial_tx');
    assert(new TextDecoder().decode(remoteDrained) === 'XYZ',
        'transfer: remote reads original writes');

    // ── Global seq ──
    console.log('Test: Global sequence');
    const g1 = port.globalChanged;  // reads and stores
    assert(!port.globalChanged, 'no change since last check');
    port.writeSlab('framebuffer', new Uint8Array([99]));
    assert(port.globalChanged, 'global changed after write');

    // ── Summary ──
    console.log(`\n${passed} passed, ${failed} failed`);
    if (failed > 0) process.exit(1);
}

run().catch(err => { console.error(err); process.exit(1); });
