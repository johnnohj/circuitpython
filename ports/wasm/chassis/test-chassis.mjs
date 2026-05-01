/**
 * test-chassis.mjs — Node.js test suite for the port chassis.
 *
 * Tests:
 *   1. MEMFS-in-linear-memory: C writes, JS reads — same bytes
 *   2. Frame loop with budget tracking
 *   3. Event ring: JS pushes events, C drains
 *   4. HAL claim/release via exports
 *   5. Serial ring buffers
 *   6. Bidirectional GPIO: JS writes → C reads, C writes → JS reads
 *   7. State persistence across frames
 */

import { readFile } from 'node:fs/promises';
import { WasiChassis } from './wasi-chassis.js';

// ── Constants (must match port_memory.h) ──

const GPIO_SLOT_SIZE = 12;
const GPIO_OFF_ENABLED = 0;
const GPIO_OFF_DIRECTION = 1;
const GPIO_OFF_VALUE = 2;
const GPIO_OFF_PULL = 3;
const GPIO_OFF_ROLE = 4;
const GPIO_OFF_FLAGS = 5;
const GPIO_OFF_CATEGORY = 6;
const GPIO_OFF_LATCHED = 7;

const ROLE_UNCLAIMED = 0x00;
const ROLE_DIGITAL_IN = 0x01;
const ROLE_DIGITAL_OUT = 0x02;
const ROLE_ADC = 0x03;

const FLAG_JS_WROTE = 0x01;
const FLAG_C_WROTE = 0x02;
const FLAG_C_READ = 0x04;

const EVT_GPIO_CHANGE = 0x01;

const EVENT_SIZE = 8;
const RING_HEADER_SIZE = 8;

// Port state offsets (port_state_t)
const STATE_OFF_PHASE = 0;
const STATE_OFF_SUBPHASE = 4;
const STATE_OFF_FRAME_COUNT = 8;
const STATE_OFF_NOW_US = 12;
const STATE_OFF_BUDGET_US = 16;
const STATE_OFF_ELAPSED_US = 20;
const STATE_OFF_STATUS = 24;
const STATE_OFF_FLAGS = 28;

// Serial ring layout
const SERIAL_RING_SIZE = 4096;
const SERIAL_DATA_OFFSET = 8;  // after write_head + read_head

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

function pushGpioEvent(mem, ringBase, pin) {
    const dv = new DataView(mem.buffer);
    const writeHead = dv.getUint32(ringBase, true);
    const evtOffset = ringBase + RING_HEADER_SIZE + writeHead;
    dv.setUint8(evtOffset + 0, EVT_GPIO_CHANGE);
    dv.setUint8(evtOffset + 1, pin);
    dv.setUint16(evtOffset + 2, 0, true);
    dv.setUint32(evtOffset + 4, 0, true);
    dv.setUint32(ringBase, writeHead + EVENT_SIZE, true);
}

async function main() {
    const stdout = [];
    const wasi = new WasiChassis({
        onStdout: (text) => { stdout.push(text); process.stdout.write(text); },
        onStderr: (text) => process.stderr.write(text),
    });

    const wasmBytes = await readFile(new URL('./build/chassis.wasm', import.meta.url));
    const { instance } = await WebAssembly.instantiate(wasmBytes, wasi.getImports());
    wasi.setInstance(instance);

    const exports = instance.exports;
    const mem = exports.memory;

    // ── Test 1: Init + MEMFS registration ──
    console.log('\n=== Test 1: Init + MEMFS registration ===');
    exports.chassis_init();

    const aliases = wasi.listFiles();
    console.log(`  Registered: ${aliases.join(', ')}`);
    assert(aliases.includes('/port/state'), '/port/state registered');
    assert(aliases.includes('/hal/gpio'), '/hal/gpio registered');
    assert(aliases.includes('/hal/analog'), '/hal/analog registered');
    assert(aliases.includes('/hal/serial/rx'), '/hal/serial/rx registered');
    assert(aliases.includes('/hal/serial/tx'), '/hal/serial/tx registered');
    assert(aliases.includes('/port/event_ring'), '/port/event_ring registered');

    // ── Test 2: Frame loop + budget tracking ──
    console.log('\n=== Test 2: Frame loop + budget tracking ===');

    const rc1 = exports.chassis_frame(16.667, 13.0);
    const stateView = wasi.readFile('/port/state');
    const stateDv = new DataView(stateView.buffer, stateView.byteOffset, stateView.byteLength);

    const frameCount = stateDv.getUint32(STATE_OFF_FRAME_COUNT, true);
    const budgetUs = stateDv.getUint32(STATE_OFF_BUDGET_US, true);
    const elapsedUs = stateDv.getUint32(STATE_OFF_ELAPSED_US, true);

    console.log(`  frame_count: ${frameCount}, budget: ${budgetUs}µs, elapsed: ${elapsedUs}µs`);
    assert(frameCount === 1, `frame_count === 1 (got ${frameCount})`);
    assert(budgetUs === 13000, `budget === 13000µs (got ${budgetUs})`);
    assert(elapsedUs < budgetUs, `elapsed (${elapsedUs}) < budget (${budgetUs})`);
    assert(rc1 === 0, `rc === PORT_RC_DONE (got ${rc1})`);

    // ── Test 3: HAL claim/release via exports ──
    console.log('\n=== Test 3: HAL claim/release ===');

    const ok1 = exports.chassis_claim_pin(5, ROLE_DIGITAL_IN);
    assert(ok1 === 1, 'claim pin 5 as DIGITAL_IN succeeds');

    const ok2 = exports.chassis_claim_pin(13, ROLE_DIGITAL_OUT);
    assert(ok2 === 1, 'claim pin 13 as DIGITAL_OUT succeeds');

    const ok3 = exports.chassis_claim_pin(5, ROLE_DIGITAL_OUT);
    assert(ok3 === 0, 'claim pin 5 as DIGITAL_OUT fails (already claimed)');

    const ok4 = exports.chassis_claim_pin(5, ROLE_DIGITAL_IN);
    assert(ok4 === 1, 'claim pin 5 as DIGITAL_IN again (idempotent) succeeds');

    // Verify claim state in MEMFS
    const gpioView = wasi.readFile('/hal/gpio');
    const pin5Role = gpioView[5 * GPIO_SLOT_SIZE + GPIO_OFF_ROLE];
    const pin13Role = gpioView[13 * GPIO_SLOT_SIZE + GPIO_OFF_ROLE];
    console.log(`  pin 5 role: ${pin5Role}, pin 13 role: ${pin13Role}`);
    assert(pin5Role === ROLE_DIGITAL_IN, `pin 5 role === DIGITAL_IN (got ${pin5Role})`);
    assert(pin13Role === ROLE_DIGITAL_OUT, `pin 13 role === DIGITAL_OUT (got ${pin13Role})`);

    // ── Test 4: C writes pin → JS reads via MEMFS ──
    console.log('\n=== Test 4: C writes → JS reads ===');

    exports.chassis_write_pin(13, 1);
    const pin13Val = gpioView[13 * GPIO_SLOT_SIZE + GPIO_OFF_VALUE];
    const pin13Flags = gpioView[13 * GPIO_SLOT_SIZE + GPIO_OFF_FLAGS];
    console.log(`  pin 13 value: ${pin13Val}, flags: 0x${pin13Flags.toString(16)}`);
    assert(pin13Val === 1, 'pin 13 value === 1');
    assert(!!(pin13Flags & FLAG_C_WROTE), 'pin 13 has C_WROTE flag');

    const readVal = exports.chassis_read_pin(13);
    assert(readVal === 1, `chassis_read_pin(13) === 1 (got ${readVal})`);

    // ── Test 5: JS writes → event → C latches ──
    console.log('\n=== Test 5: JS writes → event → C reads ===');

    // JS writes pin 5 value directly to MEMFS
    gpioView[5 * GPIO_SLOT_SIZE + GPIO_OFF_VALUE] = 1;

    // Push event to notify C
    const ringInfo = wasi.getAliasInfo('/port/event_ring');
    pushGpioEvent(mem, ringInfo.ptr, 5);

    // Run frame
    const rc2 = exports.chassis_frame(33.333, 13.0);
    console.log(`  frame rc: ${rc2}`);

    // C should have latched the value
    const latched5 = gpioView[5 * GPIO_SLOT_SIZE + GPIO_OFF_LATCHED];
    const flags5 = gpioView[5 * GPIO_SLOT_SIZE + GPIO_OFF_FLAGS];
    console.log(`  pin 5 latched: ${latched5}, flags: 0x${flags5.toString(16)}`);
    assert(latched5 === 1, 'pin 5 latched === 1');
    assert(!!(flags5 & FLAG_C_READ), 'pin 5 C_READ set');
    assert(!(flags5 & FLAG_JS_WROTE), 'pin 5 JS_WROTE cleared');

    // Read via export should return latched value (input pin)
    const readVal5 = exports.chassis_read_pin(5);
    assert(readVal5 === 1, `chassis_read_pin(5) === 1 (got ${readVal5})`);

    // ── Test 6: Serial ring buffer ──
    console.log('\n=== Test 6: Serial ring buffer ===');

    // Write data into RX ring (simulating JS keyboard input)
    const rxInfo = wasi.getAliasInfo('/hal/serial/rx');
    const rxView = wasi.readFile('/hal/serial/rx');
    const rxDv = new DataView(rxView.buffer, rxView.byteOffset, rxView.byteLength);

    // Write "Hi" into RX ring data area
    const testInput = new TextEncoder().encode('Hi');
    for (let i = 0; i < testInput.length; i++) {
        rxView[SERIAL_DATA_OFFSET + i] = testInput[i];
    }
    // Set write_head = 2 (2 bytes written)
    rxDv.setUint32(0, testInput.length, true);

    // Check TX ring — C wrote "Hello from chassis!\n" during main() test
    // (but main() runs at the very beginning, so TX may be empty in
    // browser mode where main() doesn't run — that's fine)
    const txInfo = wasi.getAliasInfo('/hal/serial/tx');
    assert(txInfo !== null, 'TX ring alias exists');
    assert(rxInfo !== null, 'RX ring alias exists');
    console.log(`  RX ring at 0x${rxInfo.ptr.toString(16)}, TX at 0x${txInfo.ptr.toString(16)}`);

    // ── Test 7: Release all + state persistence ──
    console.log('\n=== Test 7: Release all + state persistence ===');

    exports.chassis_release_all();

    const pin5RoleAfter = gpioView[5 * GPIO_SLOT_SIZE + GPIO_OFF_ROLE];
    const pin13RoleAfter = gpioView[13 * GPIO_SLOT_SIZE + GPIO_OFF_ROLE];
    assert(pin5RoleAfter === ROLE_UNCLAIMED, 'pin 5 released');
    assert(pin13RoleAfter === ROLE_UNCLAIMED, 'pin 13 released');

    // Frame count should have accumulated
    exports.chassis_frame(50.0, 13.0);
    exports.chassis_frame(66.667, 13.0);
    const fc = stateDv.getUint32(STATE_OFF_FRAME_COUNT, true);
    console.log(`  frame_count after 4 frames: ${fc}`);
    assert(fc === 4, `frame_count === 4 (got ${fc})`);

    // ── Test 8: Multiple rapid events ──
    console.log('\n=== Test 8: Multiple rapid events ===');

    exports.chassis_claim_pin(2, ROLE_DIGITAL_IN);
    exports.chassis_claim_pin(3, ROLE_DIGITAL_IN);
    exports.chassis_claim_pin(4, ROLE_DIGITAL_IN);

    // JS writes 3 pin values and pushes 3 events
    for (const pin of [2, 3, 4]) {
        gpioView[pin * GPIO_SLOT_SIZE + GPIO_OFF_VALUE] = 1;
        pushGpioEvent(mem, ringInfo.ptr, pin);
    }

    exports.chassis_frame(83.333, 13.0);

    for (const pin of [2, 3, 4]) {
        const l = gpioView[pin * GPIO_SLOT_SIZE + GPIO_OFF_LATCHED];
        assert(l === 1, `pin ${pin} latched === 1 (got ${l})`);
    }
    console.log('  All 3 pins latched in single frame');

    exports.chassis_release_all();

    // ── Test 9: Halt/resume — workload spanning multiple frames ──
    console.log('\n=== Test 9: Halt/resume — multi-frame workload ===');

    // Submit 5000 work items with a tiny budget (0.5ms) to force yields
    exports.chassis_submit_work(5000);
    assert(exports.chassis_work_active() === 1, 'work is active after submit');
    assert(exports.chassis_work_progress() === 0, 'progress starts at 0');

    let frames = 0;
    let yields = 0;
    const MAX_FRAMES = 500;  // safety limit

    while (exports.chassis_work_active() && frames < MAX_FRAMES) {
        const rc = exports.chassis_frame(100 + frames * 16.667, 0.5);  // 0.5ms budget
        frames++;
        if (rc === 1) yields++;  // PORT_RC_YIELD
    }

    const finalProgress = exports.chassis_work_progress();
    console.log(`  Completed ${finalProgress}/5000 items across ${frames} frames (${yields} yields)`);

    assert(finalProgress === 5000, `all 5000 items completed (got ${finalProgress})`);
    assert(frames > 1, `work spanned multiple frames (${frames} frames)`);
    assert(yields > 0, `at least one yield occurred (${yields} yields)`);
    assert(exports.chassis_work_active() === 0, 'work no longer active');

    // ── Test 10: Stack state visible in MEMFS ──
    console.log('\n=== Test 10: Stack state in MEMFS ===');

    const stackView = wasi.readFile('/port/stack');
    assert(stackView !== null, '/port/stack registered in MEMFS');
    if (stackView) {
        const stackDv = new DataView(stackView.buffer, stackView.byteOffset, stackView.byteLength);
        const depth = stackDv.getUint32(0, true);
        const flags = stackDv.getUint32(4, true);
        console.log(`  stack depth: ${depth}, flags: 0x${flags.toString(16)}`);
        assert(!!(flags & 0x04), 'STACK_FLAG_COMPLETE set');  // 0x04 = COMPLETE
    }

    // ── Test 11: Submit another workload — verify re-use ──
    console.log('\n=== Test 11: Re-submit workload ===');

    exports.chassis_submit_work(100);
    assert(exports.chassis_work_active() === 1, 'new work is active');
    assert(exports.chassis_work_progress() === 0, 'new work starts at 0');

    // Run with generous budget — should complete in 1 frame
    const rc3 = exports.chassis_frame(200.0, 50.0);
    const prog2 = exports.chassis_work_progress();
    console.log(`  After 1 frame (50ms budget): ${prog2}/100 items, rc=${rc3}`);
    assert(prog2 === 100, `all 100 items done in 1 frame (got ${prog2})`);
    assert(exports.chassis_work_active() === 0, 'work completed');

    // ── Test 12: Concurrent events + work ──
    console.log('\n=== Test 12: Events + work in same frame ===');

    exports.chassis_claim_pin(10, ROLE_DIGITAL_IN);
    gpioView[10 * GPIO_SLOT_SIZE + GPIO_OFF_VALUE] = 1;
    pushGpioEvent(mem, ringInfo.ptr, 10);

    exports.chassis_submit_work(50);
    const rc4 = exports.chassis_frame(250.0, 50.0);

    const pin10Latched = gpioView[10 * GPIO_SLOT_SIZE + GPIO_OFF_LATCHED];
    const workDone = exports.chassis_work_progress();
    console.log(`  pin 10 latched: ${pin10Latched}, work done: ${workDone}/50, rc: ${rc4}`);
    assert(pin10Latched === 1, 'pin 10 latched during work frame');
    assert(workDone === 50, 'work completed in same frame as events');

    exports.chassis_release_all();

    // ── Summary ──
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
    process.exit(failed > 0 ? 1 : 0);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
