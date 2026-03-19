/*
 * test_phase6c.mjs — Phase 6c: Bidirectional hardware register file
 *
 * Tests:
 *   T1: _blinka.write_reg / _blinka.read_reg round-trip
 *   T2: _blinka.pin_to_reg maps pin names correctly
 *   T3: digitalio.value getter reads from register file
 *   T4: digitalio.value setter writes to register file + emits bc_out
 *   T5: _blinka.sync_registers() reads /dev/bc_in and updates registers
 *   T6: time.sleep() emits time_sleep event + calls sync_registers
 *   T7: time.monotonic() returns a positive float
 *   T8: PythonHost.writeRegisters() → Python reads updated value
 *   T9: Module.hw.writeRegisters() direct API
 */

import { PythonHost }  from './js/PythonHost.js';
import { CHANNEL }     from './js/BroadcastBus.js';

let passed = 0;
let failed = 0;

function assert(cond, name, detail) {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name, detail ?? ''); failed++; }
}

// ── Standalone WASM tests (T1-T3, T5, T7, T9) ──────────────────────────────

{
    const { default: createModule } = await import('./build-dist/circuitpython.mjs');
    const { initializeModuleAPI }   = await import('./api.js');
    const Module = await createModule({ _workerId: 'test-6c', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

    // T1: write_reg / read_reg round-trip
    {
        const r = Module.vm.run(`
import _blinka
_blinka.write_reg(0x0E, 1)
result = _blinka.read_reg(0x0E)
print(result)
`, 500);
        assert(r.stdout.trim() === '1', 'T1: write_reg/read_reg round-trip');
    }

    // T2: pin_to_reg mapping
    {
        const r = Module.vm.run(`
import _blinka
print(_blinka.pin_to_reg("LED"))
print(_blinka.pin_to_reg("D5"))
print(_blinka.pin_to_reg("A0"))
print(_blinka.pin_to_reg("BUTTON"))
print(_blinka.pin_to_reg("UNKNOWN"))
`, 500);
        const lines = r.stdout.trim().split('\n');
        assert(lines[0] === '14',  'T2: LED=0x0E=14');
        assert(lines[1] === '5',   'T2: D5=5');
        assert(lines[2] === '16',  'T2: A0=0x10=16');
        assert(lines[3] === '15',  'T2: BUTTON=0x0F=15');
        assert(lines[4] === '-1',  'T2: UNKNOWN=-1');
    }

    // T5: sync_registers reads /dev/bc_in
    {
        // Write register state to bc_in via JS, then have Python sync
        Module.FS.writeFile('/dev/bc_in', '{"LED": 1, "D5": 1, "A0": 512}');
        const r = Module.vm.run(`
import _blinka
_blinka.sync_registers()
print(_blinka.read_reg(0x0E))
print(_blinka.read_reg(0x05))
print(_blinka.read_reg(0x10))
`, 500);
        const lines = r.stdout.trim().split('\n');
        assert(lines[0] === '1',   'T5: LED synced to 1');
        assert(lines[1] === '1',   'T5: D5 synced to 1');
        assert(lines[2] === '512', 'T5: A0 synced to 512');
    }

    // T7: time.monotonic() returns non-negative float
    {
        // Install time shim
        const { BLINKA_SHIMS } = await import('./js/shims.js');
        Module.FS.writeFile('/flash/lib/time.py', BLINKA_SHIMS['time.py']);
        const r = Module.vm.run(`
import time
t = time.monotonic()
print(t >= 0)
print(type(t).__name__)
`, 500);
        const lines = r.stdout.trim().split('\n');
        assert(lines[0] === 'True',  'T7: monotonic() >= 0');
        assert(lines[1] === 'float', 'T7: monotonic() is float');
    }

    // T9: Module.hw.writeRegisters direct API
    {
        Module.hw.writeRegisters({ LED: 0, D5: 1, A0: 1023 });
        assert(Module.hw.readReg(Module.hw.REG_LED) === 0,  'T9: LED=0 via hw API');
        assert(Module.hw.readReg(Module.hw.REG_D5) === 1,   'T9: D5=1 via hw API');
        assert(Module.hw.readReg(Module.hw.REG_A0) === 1023, 'T9: A0=1023 via hw API');
    }
}

// ── PythonHost tests (T3, T4, T6, T8) ───────────────────────────────────────

const python = new PythonHost({ executors: 1, timeout: 3000 });
await python.init();
python.installBlinka();
await new Promise(r => setTimeout(r, 300));

// T3: digitalio.value getter reads from register
{
    // Pre-set register via writeRegisters before exec
    python.writeRegisters({ D5: 1 });
    await new Promise(r => setTimeout(r, 50));  // let postMessage arrive

    const r = await python.exec(`
import _blinka
_blinka.sync_registers()
from digitalio import DigitalInOut
import board
pin = DigitalInOut(board.D5)
print(pin.value)
`);
    assert(r.stdout.trim() === 'True', 'T3: digitalio reads register (D5=1→True)', r.stdout);
}

// T4: digitalio.value setter writes register + emits bc_out
{
    const events = [];
    const unsub = python.on('hardware', ev => events.push(ev));

    const r = await python.exec(`
from digitalio import DigitalInOut, Direction
import board
led = DigitalInOut(board.LED)
led.direction = Direction.OUTPUT
led.value = True
import _blinka
reg_val = _blinka.read_reg(0x0E)
print(reg_val)
`);
    await new Promise(r => setTimeout(r, 100));
    unsub();

    assert(r.stdout.trim() === '1', 'T4: register updated to 1 after setter');
    const writeEvt = events.find(e => e.cmd === 'gpio_write' && e.pin === 'LED' && e.value === true);
    assert(writeEvt !== undefined, 'T4: gpio_write bc_out event emitted with value=true');
}

// T6: time.sleep() emits time_sleep + syncs registers
{
    // Write new register state
    python.writeRegisters({ BUTTON: 1 });
    await new Promise(r => setTimeout(r, 50));

    const events = [];
    const rxBc = new BroadcastChannel(CHANNEL);
    rxBc.onmessage = (e) => { if (e.data.type === 'hw') events.push(e.data); };

    const r = await python.exec(`
import time
time.sleep(0.1)
import _blinka
print(_blinka.read_reg(0x0F))
`);
    await new Promise(r => setTimeout(r, 50));
    rxBc.close();

    assert(r.stdout.trim() === '1', 'T6: BUTTON=1 after sync_registers in sleep()', r.stdout);
    const sleepEvt = events.find(e => e.cmd === 'time_sleep');
    assert(sleepEvt?.ms === 100, 'T6: time_sleep event with ms=100');
}

// T8: PythonHost.writeRegisters → Python reads updated value
{
    python.writeRegisters({ A0: 42 });
    await new Promise(r => setTimeout(r, 50));

    const r = await python.exec(`
import _blinka
_blinka.sync_registers()
print(_blinka.read_reg(0x10))
`);
    assert(r.stdout.trim() === '42', 'T8: A0=42 via PythonHost.writeRegisters()', r.stdout);
}

await python.shutdown();

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
