/*
 * test_phase10.mjs — Phase 10: Cooperative async scheduling (libpyasync)
 *
 * Tests:
 *   T1: time.sleep() yields to JS event loop with real timing
 *   T2: asyncio.sleep() yields to JS event loop with real timing
 *   T3: asyncio.gather() runs tasks concurrently
 *   T4: asyncio Event.wait() / Event.set() works
 *   T5: asyncio Lock works
 *   T6: runStepped timeout works
 *   T7: Multiple asyncio.sleep calls accumulate proper total delay
 *   T8: _blinka.async_sleep() directly requests delay
 */

import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));

const { default: createModule } = await import('./build-dist/circuitpython.mjs');
const { initializeModuleAPI }   = await import('./api.js');

let passed = 0;
let failed = 0;

function assert(cond, name, detail = '') {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name, detail); failed++; }
}

function readShim(name) {
    return readFileSync(join(__dirname, 'js/shims', name), 'utf8');
}

async function freshModule() {
    const Module = await createModule({ _workerId: 'test-p10', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

    // Install time shim (asyncio is frozen in the build — no need to install)
    Module.fs.writeModule('time.py', readShim('time.py'));

    return Module;
}


// T1: time.sleep() yields to JS event loop with real timing
{
    const Module = await freshModule();
    const t0 = Date.now();
    const result = await Module.vm.runStepped(`
import time
time.sleep(0.2)
print("done")
`, { budget: 64 });

    const elapsed = Date.now() - t0;
    assert(result.stdout.includes('done'), 'T1: time.sleep completes');
    assert(elapsed >= 150, 'T1: real delay occurred (>= 150ms)', `elapsed=${elapsed}ms`);
    assert(elapsed < 1000, 'T1: not excessively slow (< 1000ms)', `elapsed=${elapsed}ms`);
}


// T2: asyncio.sleep() yields to JS event loop with real timing
{
    const Module = await freshModule();
    const t0 = Date.now();
    const result = await Module.vm.runStepped(`
import asyncio

async def main():
    await asyncio.sleep(0.2)
    print("async done")

asyncio.run(main())
`, { budget: 64 });

    const elapsed = Date.now() - t0;
    assert(result.stdout.includes('async done'), 'T2: asyncio.sleep completes', `stdout='${result.stdout}'`);
    assert(elapsed >= 150, 'T2: real delay occurred (>= 150ms)', `elapsed=${elapsed}ms`);
    assert(elapsed < 1000, 'T2: not excessively slow (< 1000ms)', `elapsed=${elapsed}ms`);
}


// T3: asyncio.gather() runs tasks concurrently
{
    const Module = await freshModule();
    const t0 = Date.now();
    const result = await Module.vm.runStepped(`
import asyncio

async def task_a():
    await asyncio.sleep(0.2)
    return "a"

async def task_b():
    await asyncio.sleep(0.2)
    return "b"

async def main():
    results = await asyncio.gather(task_a(), task_b())
    print(",".join(results))

asyncio.run(main())
`, { budget: 64 });

    const elapsed = Date.now() - t0;
    assert(result.stdout.trim() === 'a,b', 'T3: gather returns both results', `stdout='${result.stdout.trim()}'`);
    // Concurrent: both sleep 200ms in parallel, total should be ~200ms not ~400ms
    assert(elapsed < 600, 'T3: concurrent execution (< 600ms)', `elapsed=${elapsed}ms`);
}


// T4: asyncio Event.wait() / Event.set()
{
    const Module = await freshModule();
    const result = await Module.vm.runStepped(`
import asyncio

event = asyncio.Event()

async def waiter():
    await event.wait()
    print("event received")

async def setter():
    await asyncio.sleep(0.1)
    event.set()

async def main():
    await asyncio.gather(waiter(), setter())

asyncio.run(main())
`, { budget: 64 });

    assert(result.stdout.includes('event received'), 'T4: Event.wait/set works', `stdout='${result.stdout}'`);
    assert(result.status === 0, 'T4: completed without error', `status=${result.status}`);
}


// T5: asyncio Lock
{
    const Module = await freshModule();
    const result = await Module.vm.runStepped(`
import asyncio

lock = asyncio.Lock()
order = []

async def worker(name, delay):
    async with lock:
        order.append(name + "_start")
        await asyncio.sleep(delay)
        order.append(name + "_end")

async def main():
    await asyncio.gather(worker("a", 0.05), worker("b", 0.05))
    print(",".join(order))

asyncio.run(main())
`, { budget: 64 });

    const order = result.stdout.trim();
    // Lock ensures serialization: a_start, a_end, b_start, b_end
    assert(order.startsWith('a_start,a_end'), 'T5: Lock serializes access', `order='${order}'`);
    assert(order.includes('b_start,b_end'), 'T5: second task completes', `order='${order}'`);
}


// T6: runStepped timeout works
{
    const Module = await freshModule();
    const t0 = Date.now();
    const result = await Module.vm.runStepped(`
while True:
    pass
`, { budget: 64, timeout: 300 });

    const elapsed = Date.now() - t0;
    assert(result.status === 2, 'T6: timeout produces exception status', `status=${result.status}`);
    assert(elapsed >= 250, 'T6: ran for near-timeout duration', `elapsed=${elapsed}ms`);
    assert(elapsed < 1000, 'T6: terminated within reason', `elapsed=${elapsed}ms`);
}


// T7: Multiple asyncio.sleep calls accumulate proper total delay
{
    const Module = await freshModule();
    const t0 = Date.now();
    const result = await Module.vm.runStepped(`
import asyncio

async def main():
    await asyncio.sleep(0.1)
    await asyncio.sleep(0.1)
    await asyncio.sleep(0.1)
    print("three sleeps done")

asyncio.run(main())
`, { budget: 64 });

    const elapsed = Date.now() - t0;
    assert(result.stdout.includes('three sleeps done'), 'T7: sequential sleeps complete');
    assert(elapsed >= 250, 'T7: total delay ~300ms (>= 250ms)', `elapsed=${elapsed}ms`);
    assert(elapsed < 1000, 'T7: not excessively slow', `elapsed=${elapsed}ms`);
}


// T8: _blinka.async_sleep() directly requests delay
{
    const Module = await freshModule();
    const t0 = Date.now();
    const result = await Module.vm.runStepped(`
import _blinka
_blinka.async_sleep(200)
print("direct sleep done")
`, { budget: 64 });

    const elapsed = Date.now() - t0;
    assert(result.stdout.includes('direct sleep done'), 'T8: _blinka.async_sleep completes');
    assert(elapsed >= 150, 'T8: real delay occurred (>= 150ms)', `elapsed=${elapsed}ms`);
}


// Summary
console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
