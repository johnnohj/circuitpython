/*
 * test_vm_yield.mjs — Step-wise VM execution (libpyvm) tests
 *
 * Tests:
 *   T1: vm.start() + vm.step() basic suspend/resume
 *   T2: Step budget controls yield frequency
 *   T3: Straight-line code completes without yielding (large budget)
 *   T4: Loop code yields multiple times (small budget)
 *   T5: Exception during stepping returns status=2
 *   T6: vm.runStepped() async driver — stdout captured
 *   T7: vm.runStepped() yields to event loop (onYield fires)
 *   T8: State persists across yields (variable set in step 1, read in step N)
 *   T9: Existing vm.run() still works (no regression from yield check)
 */

const { default: createModule } = await import('./build-dist/circuitpython.mjs');
const { initializeModuleAPI }   = await import('./api.js');

let passed = 0;
let failed = 0;

function assert(cond, name) {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name); failed++; }
}

// Create a fresh Module for each test group to avoid state leakage
async function freshModule() {
    const Module = await createModule({ _workerId: 'test-yield', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });
    return Module;
}

// ── T1: basic start + step ──────────────────────────────────────────────────

{
    const Module = await freshModule();
    const ok = Module.vm.start('x = 1 + 1');
    assert(ok === true, 'T1: start() returns true on valid code');

    // Small budget — but this is straight-line code with no branches,
    // so it might complete in one step regardless
    const status = Module.vm.step(1000);
    assert(status === 0, 'T1: simple assignment completes (status=0)');
}

// ── T2: budget controls yielding ────────────────────────────────────────────

{
    const Module = await freshModule();
    // A loop with many iterations — should yield with small budget
    Module.vm.start('s = 0\nfor i in range(1000):\n    s += i');

    // Very small budget — should yield before loop finishes
    const s1 = Module.vm.step(5);
    assert(s1 === 1, 'T2: small budget causes yield (status=1)');

    // Large budget — should complete
    const s2 = Module.vm.step(100000);
    assert(s2 === 0 || s2 === 1, 'T2: after more steps, either completes or yields again');

    // If still yielding, drain it
    let status = s2;
    let safety = 0;
    while (status === 1 && safety++ < 100) {
        status = Module.vm.step(10000);
    }
    assert(status === 0, 'T2: eventually completes (status=0)');
}

// ── T3: large budget, no yield ──────────────────────────────────────────────

{
    const Module = await freshModule();
    Module.vm.start('result = sum(range(100))');
    const status = Module.vm.step(1000000);  // huge budget
    assert(status === 0, 'T3: large budget — completes without yielding');
}

// ── T4: small budget on loop yields many times ──────────────────────────────

{
    const Module = await freshModule();
    Module.vm.start('s = 0\nfor i in range(500):\n    s += 1');

    let yields = 0;
    let status = 1;
    while (status === 1) {
        status = Module.vm.step(10);  // tiny budget
        if (status === 1) yields++;
    }
    assert(status === 0, 'T4: loop eventually completes');
    assert(yields > 5, 'T4: yielded multiple times (got ' + yields + ')');
}

// ── T5: exception during stepping ───────────────────────────────────────────

{
    const Module = await freshModule();
    Module.vm.start('x = 1/0');  // ZeroDivisionError
    const status = Module.vm.step(1000);
    assert(status === 2, 'T5: exception returns status=2');
}

// ── T6: runStepped async driver ─────────────────────────────────────────────

{
    const Module = await freshModule();
    const r = await Module.vm.runStepped('print("hello from stepped")');
    assert(r.stdout.includes('hello from stepped'), 'T6: runStepped captures stdout');
    assert(r.status === 0, 'T6: runStepped status=0');
}

// ── T7: onYield callback fires ──────────────────────────────────────────────

{
    const Module = await freshModule();
    let yieldCount = 0;
    const r = await Module.vm.runStepped(
        'for i in range(200): pass',
        { budget: 20, onYield: () => { yieldCount++; } }
    );
    assert(r.status === 0, 'T7: runStepped completes');
    assert(yieldCount > 0, 'T7: onYield fired ' + yieldCount + ' times');
}

// ── T8: state persists across yields ────────────────────────────────────────

{
    const Module = await freshModule();
    // Code sets x=0, loops to increment it
    Module.vm.start('x = 0\nfor i in range(100):\n    x += 1\nprint(x)');

    let status = 1;
    while (status === 1) {
        status = Module.vm.step(15);
    }
    assert(status === 0, 'T8: completes after multiple yields');
    // stdout should have "100"
    const stdout = Module._pyStdout || '';
    assert(stdout.includes('100'), 'T8: variable persists across yields (x=100)');
}

// ── T9: existing vm.run() still works ───────────────────────────────────────

{
    const Module = await freshModule();
    const r = Module.vm.run('print("run still works")', 2000);
    assert(r.stdout.includes('run still works'), 'T9: vm.run() unaffected by yield check');
    assert(!r.stderr, 'T9: no errors');
}

// ── Done ────────────────────────────────────────────────────────────────────

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
