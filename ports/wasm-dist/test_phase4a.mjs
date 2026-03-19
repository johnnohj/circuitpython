/**
 * test_phase4a.mjs — Phase 4 Track A: sys.settrace → /debug/trace.json
 *
 * Run with: node --experimental-vm-modules test_phase4a.mjs
 *
 * Tests:
 *   T1: enableTrace(true) then run a function — call/line/return events emitted
 *   T2: trace events contain correct file, name, lineno fields
 *   T3: enableTrace(false) stops tracing — no new events after disable
 */

import { default as createModule } from './build-dist/circuitpython.mjs';
import { initializeModuleAPI }    from './api.js';

let pass = 0, fail = 0;
function check(name, cond, detail = '') {
    if (cond) { console.log('PASS', name); pass++; }
    else       { console.log('FAIL', name, detail ? `(${detail})` : ''); fail++; }
}

// ── Init ──────────────────────────────────────────────────────────────────

const Module = await createModule({ _workerId: 'test', _workerRole: 'executor' });
initializeModuleAPI(Module);
Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

// ── T1: call/line/return events appear in trace ───────────────────────────

Module.vm.enableTrace(true);

const src = `
def add(a, b):
    return a + b
x = add(3, 4)
`;
Module.vm.run(src, 2000);

const events = Module.vm.readTrace();
console.log('Trace events:', events.length);
events.forEach(e => console.log(' ', JSON.stringify(e)));

const calls    = events.filter(e => e.event === 'call');
const lines    = events.filter(e => e.event === 'line');
const returns  = events.filter(e => e.event === 'return');

check('T1: at least one call event',   calls.length > 0,   `calls=${calls.length}`);
check('T1: at least one line event',   lines.length > 0,   `lines=${lines.length}`);
check('T1: at least one return event', returns.length > 0, `returns=${returns.length}`);

// ── T2: event fields are correct ──────────────────────────────────────────

const addCall = calls.find(e => e.name === 'add');
check('T2: call event for "add" function', !!addCall, `calls=${JSON.stringify(calls)}`);
check('T2: lineno is a number', typeof addCall?.lineno === 'number', `lineno=${addCall?.lineno}`);
check('T2: file field present', typeof addCall?.file === 'string', `file=${addCall?.file}`);

// ── T3: enableTrace(false) stops new events ───────────────────────────────

Module.vm.enableTrace(false);
const countBefore = Module.vm.readTrace().length;

Module.vm.run('y = add(1, 2)', 2000);  // add was defined above, still in scope

const countAfter = Module.vm.readTrace().length;
check('T3: no new events after disable', countAfter === countBefore,
    `before=${countBefore} after=${countAfter}`);

// ── Done ──────────────────────────────────────────────────────────────────

console.log(`\n${pass} passed, ${fail} failed`);
if (fail > 0) { process.exit(1); }
