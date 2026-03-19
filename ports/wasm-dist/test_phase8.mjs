/*
 * test_phase8.mjs — Phase 8: libpybroadcast + libpytasks tests
 *
 * Tests:
 *   T1: _blinka.send() → BroadcastChannel via ring buffer (not MEMFS)
 *   T2: Module.broadcast.send() from JS → BroadcastChannel
 *   T3: Module.tasks.register custom task, runs during vm.runStepped
 *   T4: Module.tasks.requestYield() causes step to return status=1
 *   T5: Backward compat: vm.run() still works (tasks poll transparently)
 *   T6: bc_out throughput: 500 messages in a loop, all received
 *   T7: Register sync task: JS writes registers, Python reads mid-stepped-execution
 *   T8: PythonHost with installBlinka + hardware events via ring buffer
 */

import { PythonHost }        from './js/PythonHost.js';
import { HardwareSimulator } from './js/HardwareSimulator.js';
import { CHANNEL }           from './js/BroadcastBus.js';

let passed = 0;
let failed = 0;

function assert(cond, name, detail = '') {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name, detail ? `(${detail})` : ''); failed++; }
}

// ── Standalone WASM module tests (T1–T4) ─────────────────────────────────────

async function freshModule() {
    const { default: createModule } = await import('./build-dist/circuitpython.mjs');
    const { initializeModuleAPI }   = await import('./api.js');
    const Module = await createModule({ _workerId: 'test-p8', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });
    return Module;
}

// T1: _blinka.send() → BroadcastChannel via ring buffer
{
    const Module = await freshModule();
    const events = [];
    Module._bc   = new BroadcastChannel(CHANNEL);
    const rxBc   = new BroadcastChannel(CHANNEL);
    rxBc.onmessage = (e) => { if (e.data.type === 'hw') events.push(e.data); };

    Module.vm.run('import _blinka; _blinka.send(\'{"type":"hw","cmd":"ring_test","pin":"X"}\')', 500);

    await new Promise(r => setTimeout(r, 50));
    rxBc.close();
    Module._bc.close();

    assert(events.length >= 1, 'T1: hw event via ring buffer');
    assert(events[0]?.cmd === 'ring_test', 'T1: correct cmd field');
}

// T2: Module.broadcast.send() from JS side
{
    const Module = await freshModule();
    const events = [];
    Module._bc   = new BroadcastChannel(CHANNEL);
    const rxBc   = new BroadcastChannel(CHANNEL);
    rxBc.onmessage = (e) => { if (e.data.type === 'test_bc') events.push(e.data); };

    Module.broadcast.send({ type: 'test_bc', value: 42 });
    Module.broadcast.flush();

    await new Promise(r => setTimeout(r, 50));
    rxBc.close();
    Module._bc.close();

    assert(events.length >= 1, 'T2: Module.broadcast.send() received');
    assert(events[0]?.value === 42, 'T2: correct value');
}

// T3: Custom task runs during vm.runStepped
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel(CHANNEL);  // needed for flush task
    let taskRunCount = 0;
    Module.tasks.register('test_counter', () => { taskRunCount++; });

    const r = await Module.vm.runStepped('for i in range(100): pass', { budget: 20 });

    Module._bc.close();
    assert(r.status === 0, 'T3: runStepped completes');
    assert(taskRunCount > 0, 'T3: custom task ran ' + taskRunCount + ' times');
}

// T4: Module.tasks.requestYield causes step to yield
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel(CHANNEL);

    // Register a task that requests yield after first poll
    let yieldRequested = false;
    Module.tasks.register('yield_trigger', () => {
        if (!yieldRequested) {
            yieldRequested = true;
            Module.tasks.requestYield();
        }
    });

    Module.vm.start('x = 0\nfor i in range(1000):\n    x += 1');
    // Use a very large budget — the yield should come from the task, not budget
    const status = Module.vm.step(1000000);
    Module._bc.close();

    assert(status === 1, 'T4: task-triggered yield (status=1)');
}

// T5: Backward compat — vm.run() still works
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel(CHANNEL);
    const r = Module.vm.run('print("compat test")', 2000);
    Module._bc.close();
    assert(r.stdout.includes('compat test'), 'T5: vm.run() stdout');
    assert(!r.stderr, 'T5: no errors');
}

// T6: bc_out throughput — many messages via ring buffer
{
    const Module = await freshModule();
    const events = [];
    Module._bc   = new BroadcastChannel(CHANNEL);
    const rxBc   = new BroadcastChannel(CHANNEL);
    rxBc.onmessage = (e) => { if (e.data.type === 'hw' && e.data.cmd === 'tp') events.push(e.data); };

    Module.vm.run(`
import _blinka
for i in range(500):
    _blinka.send('{"type":"hw","cmd":"tp","i":' + str(i) + '}')
`, 5000);

    await new Promise(r => setTimeout(r, 100));
    rxBc.close();
    Module._bc.close();

    assert(events.length === 500, 'T6: received all 500 messages (got ' + events.length + ')');
}

// ── PythonHost integration tests (T7–T8) ─────────────────────────────────────

// T7: Register sync via task system
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel(CHANNEL);

    // Write registers directly via Module.hw
    Module.hw.writeRegisters({ LED: 1, D5: 42 });

    // Run Python that reads the register
    const r = Module.vm.run(`
import _blinka
led_val = _blinka.read_reg(_blinka.REG_LED)
print("LED=" + str(led_val))
`, 2000);
    Module._bc.close();

    assert(r.stdout.includes('LED=1'), 'T7: register sync via task system');
}

// T8: PythonHost with installBlinka + hardware events
{
    const python = new PythonHost({ executors: 1, timeout: 3000 });
    await python.init();
    python.installBlinka();
    await new Promise(r => setTimeout(r, 200));

    const hw = new HardwareSimulator();
    hw.start();

    await python.exec(`
from digitalio import DigitalInOut, Direction
import board
led = DigitalInOut(board.LED)
led.direction = Direction.OUTPUT
led.value = True
`);
    await new Promise(r => setTimeout(r, 100));

    const pinState = hw.getPin('LED');
    hw.stop();
    await python.shutdown();

    assert(pinState !== undefined, 'T8: LED pin tracked');
    assert(pinState?.value === true, 'T8: LED value=true via ring buffer');
}

// ── Done ────────────────────────────────────────────────────────────────────

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
