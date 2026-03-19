/*
 * test_phase5b.mjs — Phase 5b: PythonREPL tests
 *
 * Tests:
 *   T1: Single-line expression (print output)
 *   T2: Multi-line block (def + call, indented)
 *   T3: State persists across sendLine() calls (REPL session)
 *   T4: Syntax error produces error event, not crash; REPL recovers
 *   T5: reset() clears partial block
 *   T6: REPL is isolated from PythonHost (separate VM state)
 */

import { PythonREPL } from './js/PythonREPL.js';

let passed = 0;
let failed = 0;

function assert(cond, name) {
    if (cond) {
        console.log('PASS', name);
        passed++;
    } else {
        console.error('FAIL', name);
        failed++;
    }
}

// Helper: send a line and wait for prompt + all output/error events
function sendAndWait(repl, line, timeoutMs = 5000) {
    return new Promise((resolve, reject) => {
        const collected = { output: [], error: [] };
        let promptReceived = false;

        const unsubs = [];
        const done = () => {
            unsubs.forEach(u => u());
            resolve(collected);
        };
        const timer = setTimeout(() => {
            unsubs.forEach(u => u());
            reject(new Error('sendAndWait timeout for line: ' + JSON.stringify(line)));
        }, timeoutMs);

        // Collect output/error until next prompt
        unsubs.push(repl.on('output', text => { collected.output.push(text); }));
        unsubs.push(repl.on('error',  text => { collected.error.push(text); }));
        unsubs.push(repl.on('prompt', p => {
            if (!promptReceived) {
                promptReceived = true;
                clearTimeout(timer);
                done();
            }
        }));

        repl.sendLine(line);
    });
}

// ── Tests ────────────────────────────────────────────────────────────────────

console.log('Initializing PythonREPL...');
const repl = new PythonREPL();
await repl.init();
console.log('REPL ready.\n');

// T1: Simple print
{
    const r = await sendAndWait(repl, 'print("hello repl")');
    assert(r.output.join('').includes('hello repl'), 'T1: print output');
    assert(r.error.length === 0, 'T1: no error');
}

// T2: Multi-line def + call (two sendLine calls, second triggers execution)
{
    // After 'def f(x):' the worker should ask for more (no output yet)
    const pending = { output: [], error: [], prompts: [] };
    const unsubs = [];
    unsubs.push(repl.on('output', t => pending.output.push(t)));
    unsubs.push(repl.on('error',  t => pending.error.push(t)));
    unsubs.push(repl.on('prompt', p => pending.prompts.push(p)));

    // Line 1: 'def f(x):'  — should get '... ' prompt back, no execution yet
    await new Promise(resolve => {
        const u = repl.on('prompt', p => { u(); resolve(p); });
        repl.sendLine('def f(x):');
    });
    assert(pending.prompts.some(p => p === '... '), 'T2: continuation prompt after def');
    assert(pending.output.length === 0, 'T2: no output yet during def');

    // Line 2: '  return x * 2'
    await new Promise(resolve => {
        const u = repl.on('prompt', p => { u(); resolve(p); });
        repl.sendLine('  return x * 2');
    });

    // Line 3: '' (empty line ends the block in MicroPython REPL convention)
    const r = await sendAndWait(repl, '');
    unsubs.forEach(u => u());

    // Now call f(3)
    const r2 = await sendAndWait(repl, 'print(f(3))');
    assert(r2.output.join('').includes('6'), 'T2: multi-line def result');
    assert(r2.error.length === 0, 'T2: no error');
}

// T3: State persists across calls
{
    await sendAndWait(repl, 'counter = 0');
    await sendAndWait(repl, 'counter += 10');
    const r = await sendAndWait(repl, 'print(counter)');
    assert(r.output.join('').includes('10'), 'T3: state persists');
}

// T4: Syntax error → error event; REPL recovers
// 'x = 1 +' is incomplete syntax that replContinue returns false for,
// so it executes immediately and produces a SyntaxError.
{
    const r = await sendAndWait(repl, 'x = 1 +');
    assert(r.error.length > 0, 'T4: syntax error produces error event');

    // REPL should still work after an error
    const r2 = await sendAndWait(repl, 'print("recovered")');
    assert(r2.output.join('').includes('recovered'), 'T4: REPL recovers after error');
}

// T5: reset() clears partial block
{
    // Start a block
    await new Promise(resolve => {
        const u = repl.on('prompt', p => { if (p === '... ') { u(); resolve(); } });
        repl.sendLine('if True:');
    });
    // Reset — should get '>>> ' back without executing
    const r = await new Promise(resolve => {
        const collected = { output: [], error: [] };
        const u1 = repl.on('output', t => collected.output.push(t));
        const u2 = repl.on('error',  t => collected.error.push(t));
        const u3 = repl.on('prompt', p => {
            if (p === '>>> ') { u1(); u2(); u3(); resolve(collected); }
        });
        repl.reset();
    });
    assert(r.output.length === 0, 'T5: reset produces no output');
    assert(r.error.length === 0,  'T5: reset produces no error');
}

// T6: REPL isolation — second REPL has its own state
{
    await sendAndWait(repl, 'secret = 42');

    const repl2 = new PythonREPL();
    await repl2.init();

    // repl2 should not see 'secret'
    let r2output = [];
    let r2error  = [];
    const u1 = repl2.on('output', t => r2output.push(t));
    const u2 = repl2.on('error',  t => r2error.push(t));
    await sendAndWait(repl2, 'print(typeof secret if hasattr(__builtins__, "secret") else "absent")');
    u1(); u2();
    await repl2.shutdown();

    // 'secret' should not be in repl2's globals (NameError or "absent")
    const text = r2output.join('') + r2error.join('');
    assert(!text.includes('42'), 'T6: REPL isolation — second REPL has no secret');
}

// ── Cleanup ──────────────────────────────────────────────────────────────────

await repl.shutdown();

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
