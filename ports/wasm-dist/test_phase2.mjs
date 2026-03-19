/**
 * test_phase2.mjs — Phase 2 integration tests
 *
 * Run with: node --experimental-vm-modules test_phase2.mjs
 *
 * Tests:
 *   P1: init workers, basic exec (delta)
 *   P2: exec with stdout capture
 *   P3: exec with stderr (exception)
 *   P4: exec with timeout abort
 *   P5: writeModule then import
 *   P6: interrupt via PythonHost.interrupt()
 *   P7: stdout event listener
 */

import { PythonHost } from './js/PythonHost.js';

let pass = 0, fail = 0;
function check(name, cond, detail = '') {
    if (cond) {
        console.log('PASS', name);
        pass++;
    } else {
        console.log('FAIL', name, detail ? `(${detail})` : '');
        fail++;
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────

console.log('Initializing PythonHost...');
const python = new PythonHost({ executors: 1, timeout: 500 });
await python.init();
console.log('Workers ready.\n');

// ── P1: basic exec ─────────────────────────────────────────────────────────

{
    const r = await python.exec('x = 1 + 1');
    check('P1 delta x==2', r.delta?.x === 2, JSON.stringify(r.delta));
    check('P1 not aborted', r.aborted === false);
}

// ── P2: stdout ─────────────────────────────────────────────────────────────

{
    const r = await python.exec('print("hello from worker")');
    check('P2 stdout', r.stdout === 'hello from worker\n', JSON.stringify(r.stdout));
}

// ── P3: exception → stderr ─────────────────────────────────────────────────

{
    const r = await python.exec('raise ValueError("worker error")');
    check('P3 stderr has ValueError', r.stderr.includes('ValueError') && r.stderr.includes('worker error'), r.stderr.slice(0, 80));
    check('P3 not aborted', r.aborted === false);
}

// ── P4: timeout abort ──────────────────────────────────────────────────────

{
    const t = Date.now();
    const r = await python.exec('while True: pass', { timeout: 300 });
    check('P4 aborted', r.aborted === true);
    check('P4 <700ms', Date.now() - t < 700, `${Date.now() - t}ms`);
}

// ── P5: writeModule + import ───────────────────────────────────────────────

{
    python.writeModule('greeting.py', 'def hello(name): return "Hi " + name\n');
    // Give the MEMFS_UPDATE a tick to be processed before the run
    await new Promise(r => setTimeout(r, 50));
    const r = await python.exec('import greeting; x = greeting.hello("world")');
    check('P5 module import', r.delta?.x === 'Hi world', JSON.stringify(r.delta));
}

// ── P6: interrupt via PythonHost.interrupt() ──────────────────────────────

{
    // Kick off a long run in background and interrupt it after 150ms
    const runPromise = python.exec('while True: pass', { timeout: 10000 });
    await new Promise(r => setTimeout(r, 150));
    python.interrupt();
    const r = await runPromise;
    check('P6 interrupt aborted', r.aborted === true);
}

// ── P7: stdout event listener ─────────────────────────────────────────────

{
    const captured = [];
    const unsub = python.on('stdout', (s) => captured.push(s));
    await python.exec('print("event test")');
    unsub();
    check('P7 stdout event', captured.some(s => s.includes('event test')), JSON.stringify(captured));
}

// ── Teardown ──────────────────────────────────────────────────────────────

await python.shutdown();
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
