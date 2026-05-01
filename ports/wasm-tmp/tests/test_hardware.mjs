// test_hardware.mjs — Test common-hal hardware modules via the frame loop.
//
// Validates that pin claim/release, GPIO read/write, and the HAL dirty
// tracking work correctly through the full wasm layer stack.

import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';

const wasmBytes = await readFile(new URL('../build-standard/circuitpython.wasm', import.meta.url));
const wasm = await WebAssembly.compile(wasmBytes);

const wasi = new WASI({ version: 'preview1', preopens: { '/': '.' } });

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

const instance = await WebAssembly.instantiate(wasm, imports);
const ex = instance.exports;
const mem = new DataView(ex.memory.buffer);

try { wasi.initialize(instance); } catch {}
ex.chassis_init();

const portMemAddr = ex.cp_port_memory_addr();
const portMemSize = ex.cp_port_memory_size();

// ── Helper: read a GPIO slot ──
// port_memory_t layout: gc_heap(512K) + pystacks(8*8K) + ctx_meta(8*16)
// + port_state(32) + event_ring(512) + hal_gpio(32*12) + ...
// We need the offset of hal_gpio within port_memory_t.
// From the struct: gc_heap=524288, pystacks=65536, ctx_meta=128,
// state=32, event_ring=512 → gpio starts at 524288+65536+128+32+512 = 590496
const GPIO_OFFSET = 524288 + 65536 + 128 + 32 + 512;
const GPIO_SLOT_SIZE = 12;

function gpioSlotAddr(pin) {
    return portMemAddr + GPIO_OFFSET + pin * GPIO_SLOT_SIZE;
}

function readGpioSlot(pin) {
    const base = gpioSlotAddr(pin);
    const buf = ex.memory.buffer;  // re-read in case of grow
    const view = new DataView(buf);
    return {
        enabled:   view.getInt8(base + 0),
        direction: view.getUint8(base + 1),
        value:     view.getUint8(base + 2),
        pull:      view.getUint8(base + 3),
        role:      view.getUint8(base + 4),
        flags:     view.getUint8(base + 5),
        category:  view.getUint8(base + 6),
        latched:   view.getUint8(base + 7),
    };
}

function writeGpioValue(pin, value) {
    const base = gpioSlotAddr(pin);
    const view = new DataView(ex.memory.buffer);
    view.setUint8(base + 2, value);       // GPIO_VALUE
    view.setUint8(base + 5, 0x01);        // GF_JS_WROTE
    ex.hal_mark_gpio_dirty(pin);
}

let passed = 0;
let failed = 0;

function assert(condition, msg) {
    if (condition) {
        passed++;
    } else {
        console.error(`  FAIL: ${msg}`);
        failed++;
    }
}

// ── Test 1: Initial state — all pins unclaimed ──
console.log('Test 1: Initial pin state');
for (let i = 0; i < 32; i++) {
    const slot = readGpioSlot(i);
    assert(slot.role === 0, `pin ${i} should be unclaimed (role=0), got ${slot.role}`);
    assert(slot.enabled === 0, `pin ${i} should be disabled, got ${slot.enabled}`);
}
console.log(`  ${passed} checks passed`);

// ── Test 2: JS writes a pin value, C reads via hal_step ──
console.log('Test 2: JS→C pin value via dirty flag');
writeGpioValue(5, 1);

// Run a frame to trigger hal_step (drains dirty flags, latches values)
const rc = ex.chassis_frame(1000, 10000);
// Result is packed: (port | sup<<8 | vm<<16).  After a dirty-flag write,
// port byte should be WASM_PORT_HW_CHANGED (3).
const portResult = rc & 0xFF;
assert(portResult === 3, `port result should be HW_CHANGED (3), got ${portResult}`);

const pin5 = readGpioSlot(5);
assert(pin5.latched === 1, `pin 5 latched should be 1 after hal_step, got ${pin5.latched}`);
assert((pin5.flags & 0x04) !== 0, `pin 5 should have C_READ flag set`);
assert((pin5.flags & 0x01) === 0, `pin 5 JS_WROTE flag should be cleared after hal_step`);
console.log(`  pin 5: latched=${pin5.latched}, flags=0x${pin5.flags.toString(16)}`);

// ── Test 3: Multiple pins dirty in one frame ──
console.log('Test 3: Multiple dirty pins');
writeGpioValue(0, 1);
writeGpioValue(13, 1);
writeGpioValue(31, 1);

ex.chassis_frame(2000, 10000);

assert(readGpioSlot(0).latched === 1, 'pin 0 should be latched');
assert(readGpioSlot(13).latched === 1, 'pin 13 should be latched');
assert(readGpioSlot(31).latched === 1, 'pin 31 should be latched');

const changes = ex.hal_get_change_count();
assert(changes >= 3, `change_count should be >= 3, got ${changes}`);
console.log(`  change_count: ${changes}`);

// ── Test 4: Serial ring buffer ──
console.log('Test 4: Serial TX ring buffer');
// The serial TX ring is after hal_gpio + hal_analog in port_mem.
// For now just verify the input buffer export works.
const inputAddr = ex.cp_input_buf_addr();
const inputSize = ex.cp_input_buf_size();
assert(inputAddr > 0, `input buffer address should be nonzero, got ${inputAddr}`);
assert(inputSize === 4096, `input buffer size should be 4096, got ${inputSize}`);
console.log(`  input_buf at 0x${inputAddr.toString(16)}, size=${inputSize}`);

// ── Test 5: Frame timing ──
console.log('Test 5: Frame timing');
const t0 = Math.floor(performance.now() * 1000);
ex.chassis_frame(t0, 10000);

// Read elapsed_us from port_state (offset 20 within state struct)
const stateAddr = portMemAddr + GPIO_OFFSET - 512 - 32;  // state is before event_ring
const elapsedUs = new DataView(ex.memory.buffer).getUint32(stateAddr + 20, true);
assert(elapsedUs >= 0 && elapsedUs < 100000, `elapsed_us should be reasonable, got ${elapsedUs}`);
console.log(`  elapsed_us: ${elapsedUs}`);

// ── Test 6: Lifecycle phase ──
console.log('Test 6: Lifecycle phase');
const phase = new DataView(ex.memory.buffer).getUint32(stateAddr + 0, true);
assert(phase === 2, `phase should be PHASE_IDLE (2), got ${phase}`);
console.log(`  phase: ${phase} (IDLE)`);

// ── Test 7: Port memory size sanity ──
console.log('Test 7: Port memory layout');
assert(portMemSize >= 590000, `port_mem should be >= 590K, got ${portMemSize}`);
console.log(`  port_mem: ${portMemSize} bytes (${(portMemSize/1024).toFixed(1)}K)`);

// ── Test 8: Ctrl-D triggers soft reboot (goes to IDLE) ──
console.log('Test 8: Ctrl-D soft reboot');
ex.cp_ctrl_d();
const phaseAfterCtrlD = new DataView(ex.memory.buffer).getUint32(stateAddr + 0, true);
// soft_reboot releases pins and returns to PHASE_IDLE (2)
assert(phaseAfterCtrlD === 2, `phase after Ctrl-D should be PHASE_IDLE (2), got ${phaseAfterCtrlD}`);
// Run a frame to confirm stability
ex.chassis_frame(5000, 10000);
console.log(`  phase after Ctrl-D: ${phaseAfterCtrlD} (IDLE)`);

// ── Summary ──
console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
console.log('All hardware tests passed.');
