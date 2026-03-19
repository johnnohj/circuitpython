/**
 * test_phase4c.mjs — Phase 4 Track C: binary VM checkpoint / restore
 *
 * Run with: node --experimental-vm-modules test_phase4c.mjs
 *
 * Tests:
 *   T1: checkpoint() writes /mem/heap and /mem/state.json
 *   T2: restore() rolls back a variable assignment
 *   T3: objects created before checkpoint are still accessible after restore
 *   T4: restore() after multiple runs still reflects checkpoint state
 */

import { default as createModule } from './build-dist/circuitpython.mjs';
import { initializeModuleAPI }    from './api.js';

let pass = 0, fail = 0;
function check(name, cond, detail = '') {
    if (cond) { console.log('PASS', name); pass++; }
    else       { console.log('FAIL', name, detail ? `(${detail})` : ''); fail++; }
}

const Module = await createModule({ _workerId: 'test', _workerRole: 'executor' });
initializeModuleAPI(Module);
Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

// ── T1: checkpoint writes /mem/ files ─────────────────────────────────────

Module.vm.run('x = 42', 1000);
Module.vm.checkpoint();

let statJson;
try {
    statJson = JSON.parse(Module.FS.readFile('/mem/state.json', { encoding: 'utf8' }));
} catch (e) { statJson = null; }

check('T1: /mem/state.json written',   !!statJson,                     `statJson=${statJson}`);
check('T1: heap_size > 0',             statJson?.heap_size > 0,        `heap_size=${statJson?.heap_size}`);
check('T1: /mem/heap exists',          Module.FS.readFile('/mem/heap').length > 0, '');

// ── T2: restore rolls back a variable assignment ──────────────────────────

// x == 42 at checkpoint; now overwrite it
Module.vm.run('x = 99', 1000);
const r_before = Module.vm.run('_check = x', 1000);
check('T2: x is 99 before restore',    r_before.delta?._check === 99,  `delta=${JSON.stringify(r_before.delta)}`);

Module.vm.restore();

const r_after = Module.vm.run('_check = x', 1000);
check('T2: x is 42 after restore',     r_after.delta?._check === 42,   `delta=${JSON.stringify(r_after.delta)}`);

// ── T3: objects created before checkpoint survive restore ─────────────────

// At checkpoint: x=42, no list yet.  Create list after checkpoint, restore, check x still works.
Module.vm.run('items = [1, 2, 3]\nx = 77', 1000);
Module.vm.restore();

const r3 = Module.vm.run('_check = x', 1000);
check('T3: x reverts to 42 after list+overwrite+restore', r3.delta?._check === 42,
    `delta=${JSON.stringify(r3.delta)}`);

// ── T4: multiple restore() calls all return to same checkpoint state ───────

for (let i = 0; i < 3; i++) {
    Module.vm.run(`x = ${100 + i}`, 1000);
    Module.vm.restore();
}
const r4 = Module.vm.run('_check = x', 1000);
check('T4: x == 42 after 3× run+restore', r4.delta?._check === 42,
    `delta=${JSON.stringify(r4.delta)}`);

// ── Done ──────────────────────────────────────────────────────────────────

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) { process.exit(1); }
