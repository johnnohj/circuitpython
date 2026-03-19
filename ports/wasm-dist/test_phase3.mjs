/**
 * test_phase3.mjs — Phase 3 integration tests: Thread → Worker mapping
 *
 * Run with: node --experimental-vm-modules test_phase3.mjs
 *
 * Tests:
 *   T1: _thread.start_new_thread() spawns a new worker
 *   T2: spawned worker runs the function with correct args
 *   T3: spawned worker stdout is captured via event listener
 *   T4: multiple threads spawn multiple workers
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

// ── Setup ──────────────────────────────────────────────────────────────────

console.log('Initializing PythonHost...');
const python = new PythonHost({ executors: 1, timeout: 2000 });
await python.init();
console.log('Workers ready.\n');

// Capture all stdout events
const allStdout = [];
python.on('stdout', (s) => allStdout.push(s));

// Track spawn events
const spawnEvents = [];
python.on('spawn', (info) => spawnEvents.push(info));

// ── Write a module for threads to use ─────────────────────────────────────

python.writeModule('thread_task.py', [
    'def run(x):',
    '    print("thread_result:" + str(x * 2))',
    '',
].join('\n'));

// Give writeModule a tick to reach workers
await new Promise(r => setTimeout(r, 50));

// ── T1: _thread.start_new_thread spawns a worker ──────────────────────────

{
    const before = spawnEvents.length;
    await python.exec([
        'import _thread',
        'import thread_task',
        '_thread.start_new_thread(thread_task.run, (21,))',
    ].join('\n'));

    // Wait for the spawned worker to start up and run
    await new Promise(r => setTimeout(r, 3000));
    check('T1 spawn event fired', spawnEvents.length > before,
        `spawn events: ${JSON.stringify(spawnEvents)}`);
}

// ── T2: spawned worker ran with correct args ───────────────────────────────

{
    const result = allStdout.join('');
    check('T2 thread result 42', result.includes('thread_result:42'),
        `stdout: ${JSON.stringify(result.slice(0, 200))}`);
}

// ── T3: spawned worker stdout captured via event listener ─────────────────

{
    // Already captured above in allStdout — just verify the stdout event fired
    const found = allStdout.some(s => s.includes('thread_result:'));
    check('T3 stdout event captured', found,
        `allStdout: ${JSON.stringify(allStdout)}`);
}

// ── T4: multiple threads spawn multiple workers ───────────────────────────

{
    const beforeCount = spawnEvents.length;
    await python.exec([
        'import _thread',
        'import thread_task',
        '_thread.start_new_thread(thread_task.run, (10,))',
        '_thread.start_new_thread(thread_task.run, (20,))',
    ].join('\n'));

    await new Promise(r => setTimeout(r, 4000));
    check('T4 two spawns fired', spawnEvents.length >= beforeCount + 2,
        `total spawn events: ${spawnEvents.length}, before: ${beforeCount}`);

    const result = allStdout.join('');
    check('T4 result 20 present', result.includes('thread_result:20'), result.slice(0, 300));
    check('T4 result 40 present', result.includes('thread_result:40'), result.slice(0, 300));
}

// ── Teardown ──────────────────────────────────────────────────────────────

await python.shutdown();
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
