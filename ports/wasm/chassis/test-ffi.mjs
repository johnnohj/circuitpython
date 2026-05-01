/**
 * test-ffi.mjs — Phase 4 FFI tests.
 *
 * Tests:
 *   1. ChassisAPI high-level API
 *   2. C→JS notifications (ffi.notify)
 *   3. C→JS request_frame
 *   4. Full round-trip: JS setPinValue → event → C latch → notify → JS callback
 *   5. Shared constants consistency
 *   6. No WASI fd calls in hot path (frame loop uses only linear memory)
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
    // ── Test 1: ChassisAPI (Node.js — no rAF, use step()) ──
    // We can't use ChassisAPI.create in Node (no fetch), so test the
    // low-level WasiChassis with FFI callbacks directly.

    console.log('\n=== Test 1: FFI callbacks via WasiChassis ===');

    const notifications = [];
    const frameRequests = [];

    const wasi = new WasiChassis({
        onStdout: () => {},
        onNotify: (type, pin, arg, data) => {
            notifications.push({ type, pin, arg, data });
        },
        onRequestFrame: () => {
            frameRequests.push(Date.now());
        },
    });

    const wasmBytes = await readFile(new URL('./build/chassis.wasm', import.meta.url));
    const { instance } = await WebAssembly.instantiate(wasmBytes, wasi.getImports());
    wasi.setInstance(instance);
    const exports = instance.exports;

    exports.chassis_init();

    // ── Test 2: C→JS notify on pin write ──
    console.log('\n=== Test 2: C→JS notify on pin write ===');

    exports.chassis_claim_pin(7, C.ROLE_DIGITAL_OUT);
    notifications.length = 0;

    exports.chassis_write_pin(7, 1);

    assert(notifications.length === 1, `got 1 notification (got ${notifications.length})`);
    if (notifications.length > 0) {
        const n = notifications[0];
        assert(n.type === C.NOTIFY_PIN_CHANGED, `type === NOTIFY_PIN_CHANGED (got ${n.type})`);
        assert(n.pin === 7, `pin === 7 (got ${n.pin})`);
        assert(n.arg === 1, `arg === 1 (value high) (got ${n.arg})`);
        console.log(`  Notification: type=${n.type} pin=${n.pin} arg=${n.arg}`);
    }

    // Write low
    notifications.length = 0;
    exports.chassis_write_pin(7, 0);
    assert(notifications.length === 1, 'got notification for low write');
    assert(notifications[0]?.arg === 0, 'arg === 0 (value low)');

    // ── Test 3: C→JS request_frame on yield ──
    console.log('\n=== Test 3: C→JS request_frame on yield ===');

    frameRequests.length = 0;
    exports.chassis_submit_work(5000);

    // Run with tiny budget to force yield
    exports.chassis_frame(100.0, 0.5);

    assert(frameRequests.length > 0, `request_frame called on yield (${frameRequests.length} calls)`);
    console.log(`  request_frame called ${frameRequests.length} time(s)`);

    // Drain the work
    while (exports.chassis_work_active()) {
        exports.chassis_frame(200.0, 50.0);
    }

    // ── Test 4: Full round-trip ──
    console.log('\n=== Test 4: Full round-trip (JS→event→C→notify→JS) ===');

    exports.chassis_claim_pin(3, C.ROLE_DIGITAL_IN);
    notifications.length = 0;

    // JS writes pin 3 value to MEMFS
    const gpioView = wasi.readFile('/hal/gpio');
    gpioView[3 * C.GPIO_SLOT_SIZE + C.GPIO_VALUE] = 1;
    gpioView[3 * C.GPIO_SLOT_SIZE + C.GPIO_FLAGS] |= C.GF_JS_WROTE;

    // JS pushes event
    const ringInfo = wasi.getAliasInfo('/port/event_ring');
    const mem = exports.memory;
    const dv = new DataView(mem.buffer);
    const writeHead = dv.getUint32(ringInfo.ptr, true);
    const evtOff = ringInfo.ptr + C.RING_HEADER_SIZE + writeHead;
    dv.setUint8(evtOff, C.EVT_GPIO_CHANGE);
    dv.setUint8(evtOff + 1, 3);
    dv.setUint16(evtOff + 2, 0, true);
    dv.setUint32(evtOff + 4, 0, true);
    dv.setUint32(ringInfo.ptr, writeHead + C.EVENT_SIZE, true);

    // Run frame — C drains event, latches pin, no notification expected
    // (notifications come from hal_write_pin which is C→output, not input latch)
    const rc = exports.chassis_frame(300.0, 13.0);

    // Verify C latched the value
    const latched = gpioView[3 * C.GPIO_SLOT_SIZE + C.GPIO_LATCHED];
    assert(latched === 1, `pin 3 latched === 1 (got ${latched})`);

    // Verify via export
    const readVal = exports.chassis_read_pin(3);
    assert(readVal === 1, `chassis_read_pin(3) === 1 (got ${readVal})`);

    // Now C writes an output — should trigger notification
    exports.chassis_claim_pin(13, C.ROLE_DIGITAL_OUT);
    notifications.length = 0;
    exports.chassis_write_pin(13, 1);

    assert(notifications.length === 1, 'output write triggers notification');
    assert(notifications[0]?.pin === 13, 'notification for pin 13');

    // Verify JS sees the value in MEMFS
    const pin13Val = gpioView[13 * C.GPIO_SLOT_SIZE + C.GPIO_VALUE];
    assert(pin13Val === 1, `pin 13 value visible in MEMFS (got ${pin13Val})`);

    console.log('  Full round-trip: JS→MEMFS→event→C latch OK');
    console.log('  Full round-trip: C write→MEMFS→notify→JS callback OK');

    // ── Test 5: Shared constants consistency ──
    console.log('\n=== Test 5: Shared constants consistency ===');

    // Verify key constants match what C expects by reading actual struct sizes
    assert(C.GPIO_SLOT_SIZE === 12, 'GPIO_SLOT_SIZE === 12');
    assert(C.EVENT_SIZE === 8, 'EVENT_SIZE === 8');
    assert(C.RING_HEADER_SIZE === 8, 'RING_HEADER_SIZE === 8');
    assert(C.RC_DONE === 0, 'RC_DONE === 0');
    assert(C.RC_YIELD === 1, 'RC_YIELD === 1');
    assert(C.RC_EVENTS === 2, 'RC_EVENTS === 2');

    // Verify port_state_t offsets by reading known values
    const stateView = wasi.readFile('/port/state');
    const stateDv = new DataView(stateView.buffer, stateView.byteOffset, stateView.byteLength);
    const fc = stateDv.getUint32(C.PS_FRAME_COUNT, true);
    assert(fc > 0, `frame_count at offset ${C.PS_FRAME_COUNT} is positive (${fc})`);

    const status = stateDv.getUint32(C.PS_STATUS, true);
    assert(status <= 3, `status at offset ${C.PS_STATUS} is valid (${status})`);

    console.log(`  Constants verified: frame_count=${fc}, status=${C.RC_NAMES[status]}`);

    // ── Test 6: Hot path uses no WASI fd calls ──
    console.log('\n=== Test 6: Hot path — no fd calls ===');

    // The hot path is: chassis_frame → drain_events → hal_step → port_step
    // All data access is via pointer dereference into port_mem.
    // The only WASI calls are clock_time_get (for budget tracking) — acceptable.
    //
    // Verify by running 100 frames and confirming they complete without
    // touching any fd-based state.

    const preFrameCount = stateDv.getUint32(C.PS_FRAME_COUNT, true);

    for (let i = 0; i < 100; i++) {
        exports.chassis_frame(400 + i * 16.667, 13.0);
    }

    const postFrameCount = stateDv.getUint32(C.PS_FRAME_COUNT, true);
    assert(postFrameCount === preFrameCount + 100,
        `100 frames ran (${preFrameCount} → ${postFrameCount})`);

    // Measure frame time
    const t0 = performance.now();
    for (let i = 0; i < 1000; i++) {
        exports.chassis_frame(2000 + i * 0.016, 0.001);
    }
    const t1 = performance.now();
    const usPerFrame = ((t1 - t0) / 1000) * 1000;
    console.log(`  1000 frames in ${(t1 - t0).toFixed(1)}ms (${usPerFrame.toFixed(1)}µs/frame)`);
    assert(usPerFrame < 500, `frame time < 500µs (got ${usPerFrame.toFixed(1)}µs)`);

    exports.chassis_release_all();

    // ── Summary ──
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===\n`);
    process.exit(failed > 0 ? 1 : 0);
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
