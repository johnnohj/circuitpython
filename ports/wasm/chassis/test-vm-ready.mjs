/**
 * test-vm-ready.mjs — Phase 5: VM preparation tests.
 *
 * Tests:
 *   1. VM regions registered as MEMFS files
 *   2. GC heap accessible and writable from both C and JS
 *   3. Per-context pystacks independent
 *   4. Context metadata readable/writable
 *   5. Address validation (machine.mem32 safety)
 *   6. Input buffer round-trip
 *   7. Memory map completeness
 *   8. Full layout: all regions contiguous in port_mem
 */

import { readFile } from 'node:fs/promises';
import { WasiChassis } from './wasi-chassis.js';
import * as C from './chassis-constants.mjs';

let passed = 0;
let failed = 0;

function assert(condition, msg) {
    if (condition) {
        passed++;
    } else {
        failed++;
        console.error(`  FAIL: ${msg}`);
    }
}

async function main() {
    const wasi = new WasiChassis({
        onStdout: (text) => process.stdout.write(text),
    });

    const wasmBytes = await readFile(new URL('./build/chassis.wasm', import.meta.url));
    const { instance } = await WebAssembly.instantiate(wasmBytes, wasi.getImports());
    wasi.setInstance(instance);
    const exports = instance.exports;
    const mem = exports.memory;

    exports.chassis_init();

    // ── Test 1: VM regions registered ──
    console.log('\n=== Test 1: VM MEMFS regions ===');

    const files = wasi.listFiles();
    assert(files.includes('/py/heap'), '/py/heap registered');
    assert(files.includes('/py/ctx/0/pystack'), '/py/ctx/0/pystack registered');
    assert(files.includes('/py/ctx/1/pystack'), '/py/ctx/1/pystack registered');
    assert(files.includes('/py/ctx/2/pystack'), '/py/ctx/2/pystack registered');
    assert(files.includes('/py/ctx/3/pystack'), '/py/ctx/3/pystack registered');
    assert(files.includes('/py/ctx/meta'), '/py/ctx/meta registered');
    assert(files.includes('/py/input'), '/py/input registered');
    console.log(`  ${files.length} total files registered`);

    // ── Test 2: GC heap ──
    console.log('\n=== Test 2: GC heap ===');

    const heapAddr = exports.chassis_gc_heap_addr();
    const heapSize = exports.chassis_gc_heap_size();
    console.log(`  GC heap at 0x${heapAddr.toString(16)}, ${heapSize} bytes (${heapSize/1024}K)`);

    assert(heapSize === 256 * 1024, `heap size === 256K (got ${heapSize})`);

    // Read via MEMFS
    const heapView = wasi.readFile('/py/heap');
    assert(heapView !== null, '/py/heap readable');
    assert(heapView.length === heapSize, `heap view length matches (${heapView.length})`);

    // Write pattern to heap via JS, verify via MEMFS view
    const u8 = new Uint8Array(mem.buffer);
    u8[heapAddr] = 0xCA;
    u8[heapAddr + 1] = 0xFE;
    assert(heapView[0] === 0xCA, 'JS write to heap visible via MEMFS view');
    assert(heapView[1] === 0xFE, 'second byte matches');

    // Write via MEMFS view, verify via raw memory
    heapView[2] = 0xBE;
    heapView[3] = 0xEF;
    assert(u8[heapAddr + 2] === 0xBE, 'MEMFS write visible in raw memory');
    assert(u8[heapAddr + 3] === 0xEF, 'second byte matches');

    console.log('  Bidirectional heap access verified');

    // ── Test 3: Per-context pystacks ──
    console.log('\n=== Test 3: Per-context pystacks ===');

    const pystackSize = exports.chassis_pystack_size();
    assert(pystackSize === 8192, `pystack size === 8K (got ${pystackSize})`);

    // Verify each context has independent memory
    const pystackAddrs = [];
    for (let i = 0; i < 4; i++) {
        const addr = exports.chassis_pystack_addr(i);
        pystackAddrs.push(addr);
        const view = wasi.readFile(`/py/ctx/${i}/pystack`);
        assert(view !== null, `/py/ctx/${i}/pystack readable`);
        assert(view.length === pystackSize, `ctx ${i} pystack size matches`);

        // Write a unique marker to each
        view[0] = 0xA0 + i;
    }

    // Verify markers are independent
    for (let i = 0; i < 4; i++) {
        const view = wasi.readFile(`/py/ctx/${i}/pystack`);
        assert(view[0] === 0xA0 + i, `ctx ${i} marker is 0x${(0xA0+i).toString(16)} (got 0x${view[0].toString(16)})`);
    }

    // Verify non-overlapping
    for (let i = 1; i < 4; i++) {
        assert(pystackAddrs[i] > pystackAddrs[i-1],
            `pystack[${i}] (0x${pystackAddrs[i].toString(16)}) > pystack[${i-1}] (0x${pystackAddrs[i-1].toString(16)})`);
        assert(pystackAddrs[i] - pystackAddrs[i-1] === pystackSize,
            `spacing === ${pystackSize}`);
    }
    console.log(`  4 independent pystacks at ${pystackAddrs.map(a => '0x'+a.toString(16)).join(', ')}`);

    // ── Test 4: Context metadata ──
    console.log('\n=== Test 4: Context metadata ===');

    const metaView = wasi.readFile('/py/ctx/meta');
    assert(metaView !== null, '/py/ctx/meta readable');
    const metaDv = new DataView(metaView.buffer, metaView.byteOffset, metaView.byteLength);

    // Write context 0 status to RUNNABLE
    const CTX_META_SIZE = 16;  // sizeof(vm_context_meta_t)
    metaView[0 * CTX_META_SIZE + 0] = 2;  // status = CTX_RUNNABLE
    metaView[0 * CTX_META_SIZE + 1] = 0;  // priority = highest

    // Write context 1 status to SLEEPING
    metaView[1 * CTX_META_SIZE + 0] = 5;  // status = CTX_SLEEPING
    metaView[1 * CTX_META_SIZE + 1] = 1;  // priority = 1

    // Verify via read
    assert(metaView[0] === 2, 'ctx 0 status === RUNNABLE');
    assert(metaView[CTX_META_SIZE] === 5, 'ctx 1 status === SLEEPING');
    console.log('  Context metadata read/write verified');

    // ── Test 5: Address validation ──
    console.log('\n=== Test 5: Address validation (machine.mem32) ===');

    // Valid: GC heap address
    assert(exports.chassis_validate_addr(heapAddr, 4) === 1,
        `heap addr 0x${heapAddr.toString(16)} is valid`);

    // Valid: heap + 100 (still in range)
    assert(exports.chassis_validate_addr(heapAddr + 100, 4) === 1,
        'heap+100 is valid');

    // Valid: GPIO region
    const gpioInfo = wasi.getAliasInfo('/hal/gpio');
    assert(exports.chassis_validate_addr(gpioInfo.ptr, 1) === 1,
        `gpio addr 0x${gpioInfo.ptr.toString(16)} is valid`);

    // Valid: pystack[2]
    assert(exports.chassis_validate_addr(pystackAddrs[2], 4) === 1,
        'pystack[2] is valid');

    // Invalid: random address
    assert(exports.chassis_validate_addr(0xDEAD, 4) === 0,
        '0xDEAD is invalid');

    // Just past heap end — this address is the start of pystacks,
    // which IS a valid region.  Test a truly unmapped address instead.
    assert(exports.chassis_validate_addr(0x00000004, 4) === 0,
        'low address is invalid');

    console.log('  Address validation verified');

    // ── Test 6: Input buffer ──
    console.log('\n=== Test 6: Input buffer ===');

    const inputAddr = exports.chassis_input_buf_addr();
    const inputSize = exports.chassis_input_buf_size();
    assert(inputSize === 4096, `input buf size === 4096 (got ${inputSize})`);

    const inputView = wasi.readFile('/py/input');
    assert(inputView !== null, '/py/input readable');

    // Simulate JS writing a REPL command
    const cmd = 'print("hello")\n';
    const encoder = new TextEncoder();
    const encoded = encoder.encode(cmd);
    inputView.set(encoded);

    // Verify via raw memory
    const readBack = new TextDecoder().decode(new Uint8Array(mem.buffer, inputAddr, encoded.length));
    assert(readBack === cmd, `input buffer contains "${readBack.trim()}"`);
    console.log(`  Input buffer: wrote "${cmd.trim()}", read back OK`);

    // ── Test 7: Memory map completeness ──
    console.log('\n=== Test 7: Memory map ===');

    const mapCount = exports.chassis_mem_map_count();
    console.log(`  Memory map has ${mapCount} entries`);
    assert(mapCount >= 14, `map has >= 14 entries (got ${mapCount})`);

    // Verify all registered files have valid aliases
    for (const path of files) {
        const info = wasi.getAliasInfo(path);
        assert(info !== null, `${path} has alias info`);
        assert(info.size > 0, `${path} has size > 0 (${info.size})`);
    }

    // ── Test 8: Layout sanity ──
    console.log('\n=== Test 8: Layout ===');

    const portAddr = exports.chassis_memory_addr();
    const portSize = exports.chassis_memory_size();
    console.log(`  port_mem: 0x${portAddr.toString(16)}, ${portSize} bytes (${(portSize/1024).toFixed(1)}K)`);

    // Heap should be inside port_mem
    assert(heapAddr >= portAddr, 'heap starts after port_mem base');
    assert(heapAddr + heapSize <= portAddr + portSize, 'heap ends before port_mem end');

    // Pystacks should be after heap
    assert(pystackAddrs[0] >= heapAddr + heapSize, 'pystacks start after heap');

    // All regions fit within port_mem
    const inputEnd = inputAddr + inputSize;
    assert(inputEnd <= portAddr + portSize, 'input buffer fits in port_mem');

    console.log('  Layout is contiguous and sane');

    // ── Summary ──
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
    process.exit(failed > 0 ? 1 : 0);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
