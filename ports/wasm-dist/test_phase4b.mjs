/**
 * test_phase4b.mjs — Phase 4 Track B: .mpy compilation
 *
 * Run with: node --experimental-vm-modules test_phase4b.mjs
 *
 * Tests:
 *   T1: compile() sends COMPILE_REQUEST and resolves with a .mpy path
 *   T2: compiled .mpy is loaded by executor: import + call returns correct result
 *   T3: compile error falls back to .py path with error message
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

console.log('Initializing PythonHost (1 executor, 1 compiler)...');
const python = new PythonHost({ executors: 1, compilers: 1, timeout: 5000 });
await python.init();
console.log('Workers ready.\n');

// ── T1: compile() resolves with .mpy path ─────────────────────────────────

const r1 = await python.compile('mymod', 'def add(a, b): return a + b');
check('T1: compile() returns .mpy path',
    r1.path === '/flash/mymod.mpy',
    `path=${r1.path}`);
check('T1: no error', !r1.error, `error=${r1.error}`);

// ── T2: executor can import and call the compiled module ───────────────────

// Brief pause to allow MEMFS_UPDATE to reach executor
await new Promise(r => setTimeout(r, 100));

const r2 = await python.exec('import mymod\nx = mymod.add(3, 4)');
check('T2: delta.x == 7', r2.delta?.x === 7, `delta=${JSON.stringify(r2.delta)}`);
check('T2: no stderr', !r2.stderr, `stderr=${r2.stderr}`);

// ── T3: compile error falls back to .py path ──────────────────────────────

const r3 = await python.compile('badmod', 'def oops(: syntax error here');
check('T3: error on bad source', !!r3.error, `error=${r3.error}`);
check('T3: fallback path is .py', r3.path === '/flash/badmod.py', `path=${r3.path}`);

// ── Done ──────────────────────────────────────────────────────────────────

await python.shutdown();
console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) { process.exit(1); }
