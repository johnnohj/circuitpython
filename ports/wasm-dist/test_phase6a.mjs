/*
 * test_phase6a.mjs — Phase 6a: blinka shims + HardwareSimulator tests
 *
 * Tests:
 *   T1: _blinka.send() delivers a hw event on BroadcastChannel
 *   T2: digitalio.DigitalInOut gpio_init + gpio_write events
 *   T3: neopixel.NeoPixel neo_init + neo_write events
 *   T4: HardwareSimulator state — gpio_write updates pin state
 *   T5: HardwareSimulator state — neo_write updates pixel array
 *   T6: PythonHost.installBlinka() + integrated LED blink
 */

import { PythonHost }        from './js/PythonHost.js';
import { HardwareSimulator } from './js/HardwareSimulator.js';
import { CHANNEL }           from './js/BroadcastBus.js';

let passed = 0;
let failed = 0;

function assert(cond, name) {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name); failed++; }
}

// Helper: run Python, collect hw events from BroadcastChannel
function runAndCollectHW(python, code, timeoutMs = 4000) {
    return new Promise((resolve, reject) => {
        const events = [];
        const unsub = python.on('hardware', ev => events.push(ev));
        const timer = setTimeout(() => { unsub(); resolve(events); }, timeoutMs);

        python.exec(code).then(result => {
            // Give BC a tick to deliver any trailing events
            setTimeout(() => {
                clearTimeout(timer);
                unsub();
                resolve({ events, result });
            }, 100);
        }).catch(err => {
            clearTimeout(timer);
            unsub();
            reject(err);
        });
    });
}

// ── Standalone WASM module tests (T1) ────────────────────────────────────────

{
    const { default: createModule } = await import('./build-dist/circuitpython.mjs');
    const { initializeModuleAPI }   = await import('./api.js');
    const Module = await createModule({ _workerId: 'test-6a', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

    // T1: _blinka.send() writes to bc_out; after run() the drain broadcasts it.
    // A BC sender does NOT receive its own messages, so we need a second channel instance.
    {
        const events = [];
        Module._bc   = new BroadcastChannel(CHANNEL);  // sender (used by drain)
        const rxBc   = new BroadcastChannel(CHANNEL);  // separate receiver
        rxBc.onmessage = (e) => { if (e.data.type === 'hw') { events.push(e.data); } };

        Module.vm.run('import _blinka; _blinka.send(\'{"type":"hw","cmd":"test_ping","pin":"X"}\')', 500);

        await new Promise(r => setTimeout(r, 50));
        rxBc.close();
        Module._bc.close();

        assert(events.length >= 1,             'T1: hw event delivered on BroadcastChannel');
        assert(events[0]?.cmd === 'test_ping', 'T1: event has correct cmd');
    }
}

// ── PythonHost tests (T2–T6) ──────────────────────────────────────────────────

const python = new PythonHost({ executors: 1, timeout: 2000 });
await python.init();
python.installBlinka();

// Give workers a moment to receive the writeModule messages
await new Promise(r => setTimeout(r, 200));

// T2: digitalio — gpio_init + gpio_write
{
    const { events } = await runAndCollectHW(python, `
from digitalio import DigitalInOut, Direction
import board
led = DigitalInOut(board.LED)
led.direction = Direction.OUTPUT
led.value = True
led.value = False
`);
    const cmds = events.map(e => e.cmd);
    assert(cmds.includes('gpio_init'),  'T2: gpio_init event emitted');
    assert(cmds.includes('gpio_write'), 'T2: gpio_write event emitted');
    const writeEvents = events.filter(e => e.cmd === 'gpio_write');
    assert(writeEvents.some(e => e.value === true),  'T2: gpio_write value=true');
    assert(writeEvents.some(e => e.value === false), 'T2: gpio_write value=false');
}

// T3: neopixel — neo_init + neo_write
{
    const { events } = await runAndCollectHW(python, `
import neopixel, board
pixels = neopixel.NeoPixel(board.D10, 3)
pixels.fill((10, 20, 30))
`);
    const cmds = events.map(e => e.cmd);
    assert(cmds.includes('neo_init'),  'T3: neo_init event emitted');
    assert(cmds.includes('neo_write'), 'T3: neo_write event emitted');
    const write = events.find(e => e.cmd === 'neo_write');
    assert(write?.pixels?.length === 3,        'T3: pixels array length=3');
    assert(write?.pixels?.[0]?.[0] === 10,     'T3: pixel[0][0]=10');
}

// T4: HardwareSimulator GPIO state
{
    const hw = new HardwareSimulator();
    hw.start();

    await python.exec(`
from digitalio import DigitalInOut, Direction
import board
pin = DigitalInOut(board.D5)
pin.direction = Direction.OUTPUT
pin.value = True
`);
    await new Promise(r => setTimeout(r, 150));

    const state = hw.getPin('D5');
    assert(state !== undefined,                  'T4: pin D5 is tracked');
    assert(state?.direction === 'output',        'T4: direction = output');
    assert(state?.value === true,                'T4: value = true');
    hw.stop();
}

// T5: HardwareSimulator NeoPixel state
{
    const hw = new HardwareSimulator();
    hw.start();

    await python.exec(`
import neopixel, board
np = neopixel.NeoPixel(board.D10, 4, auto_write=False)
np[0] = (255, 0, 0)
np[1] = (0, 255, 0)
np.show()
`);
    await new Promise(r => setTimeout(r, 150));

    const np = hw.getNeoPixel('D10');
    assert(np !== undefined,               'T5: D10 NeoPixel tracked');
    assert(np?.n === 4,                    'T5: n=4');
    assert(np?.pixels?.[0]?.[0] === 255,   'T5: pixel[0]=[255,0,0]');
    assert(np?.pixels?.[1]?.[1] === 255,   'T5: pixel[1]=[0,255,0]');
    hw.stop();
}

// T6: integrated LED blink — no errors
{
    const r = await python.exec(`
from digitalio import DigitalInOut, Direction
import board
led = DigitalInOut(board.LED)
led.direction = Direction.OUTPUT
for _ in range(3):
    led.value = True
    led.value = False
print("blink done")
`);
    assert(r.stdout.includes('blink done'), 'T6: blink loop runs to completion');
    assert(!r.stderr,                       'T6: no errors');
}

await python.shutdown();

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
