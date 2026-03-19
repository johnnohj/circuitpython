/*
 * test_phase9.mjs — Phase 9: GC heap + pystack as addressable resources
 *
 * Tests:
 *   T1: Module.gc.stats() returns sane values
 *   T2: Module.gc.snapshot() returns Uint8Array of correct length
 *   T3: Checkpoint/restore round-trip: set x=42, checkpoint, set x=0, restore, verify x=42
 *   T4: Module.pystack.stats() returns sane values
 *   T5: Module.pystack.snapshot() during stepping has non-zero used
 *   T6: gc.stats() changes after Python allocations
 *   T7: gc.blobUrl() returns a blob: URL
 *   T8: gc.checkpoint/restoreCheckpoint (heap + stateCtx bundle)
 */

const { default: createModule } = await import('./build-dist/circuitpython.mjs');
const { initializeModuleAPI }   = await import('./api.js');

let passed = 0;
let failed = 0;

function assert(cond, name, detail = '') {
    if (cond) { console.log('PASS', name); passed++; }
    else       { console.error('FAIL', name, detail); failed++; }
}

async function freshModule() {
    const Module = await createModule({ _workerId: 'test-p9', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });
    return Module;
}

// T1: gc.stats()
{
    const Module = await freshModule();
    const s = Module.gc.stats();
    assert(typeof s.used === 'number' && s.used >= 0,    'T1: gc.stats().used is a number');
    assert(typeof s.total === 'number' && s.total > 0,   'T1: gc.stats().total > 0');
    assert(typeof s.free === 'number' && s.free > 0,     'T1: gc.stats().free > 0');
    assert(s.used + s.free === s.total,                  'T1: used + free = total');
}

// T2: gc.snapshot() length
{
    const Module = await freshModule();
    const snap = Module.gc.snapshot();
    assert(snap instanceof Uint8Array,  'T2: snapshot is Uint8Array');
    const size = Module.ccall('mp_gc_heap_size', 'number', [], []);
    assert(snap.length === size,        'T2: snapshot length matches heap size (' + snap.length + ' vs ' + size + ')');
}

// T3: checkpoint/restore round-trip
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel('test-p9');

    // Set x=42
    Module.vm.run('x = 42', 1000);

    // Checkpoint
    const cp = Module.gc.checkpoint();

    // Overwrite x
    Module.vm.run('x = 0', 1000);
    let r = Module.vm.run('print(x)', 1000);
    assert(r.stdout.trim() === '0', 'T3: x=0 after overwrite');

    // Restore
    Module.gc.restoreCheckpoint(cp);
    r = Module.vm.run('print(x)', 1000);
    assert(r.stdout.trim() === '42', 'T3: x=42 after restore');

    Module._bc.close();
}

// T4: pystack.stats()
{
    const Module = await freshModule();
    const s = Module.pystack.stats();
    assert(typeof s.used === 'number',                   'T4: pystack.stats().used is a number');
    assert(typeof s.total === 'number' && s.total > 0,   'T4: pystack.stats().total > 0');
    assert(s.free >= 0,                                  'T4: pystack.stats().free >= 0');
    assert(s.used + s.free === s.total,                  'T4: used + free = total');
}

// T5: pystack used > 0 during stepping
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel('test-p9');
    Module.vm.start('for i in range(100): pass');
    const status = Module.vm.step(10);  // yield mid-execution
    const s = Module.pystack.stats();
    assert(status === 1, 'T5: stepping yields');
    assert(s.used > 0,   'T5: pystack used > 0 during stepping (' + s.used + ')');
    // Drain remaining
    let st = status;
    while (st === 1) { st = Module.vm.step(10000); }
    Module._bc.close();
}

// T6: gc.stats() changes after allocations
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel('test-p9');
    const before = Module.gc.stats();
    Module.vm.run('big_list = list(range(1000))', 2000);
    const after = Module.gc.stats();
    assert(after.used > before.used, 'T6: gc used increased after allocation (' + before.used + ' → ' + after.used + ')');
    Module._bc.close();
}

// T7: gc.blobUrl()
{
    const Module = await freshModule();
    const url = Module.gc.blobUrl();
    assert(typeof url === 'string', 'T7: blobUrl returns string');
    assert(url.startsWith('blob:'),  'T7: URL starts with blob:');
    URL.revokeObjectURL(url);
}

// T8: gc.checkpoint + restoreCheckpoint bundle
{
    const Module = await freshModule();
    Module._bc = new BroadcastChannel('test-p9');

    Module.vm.run('state_var = "hello"', 1000);
    const cp = Module.gc.checkpoint();

    assert(cp.heap instanceof Uint8Array,     'T8: checkpoint.heap is Uint8Array');
    assert(cp.stateCtx instanceof Uint8Array, 'T8: checkpoint.stateCtx is Uint8Array');
    assert(cp.stateCtx.length > 0,           'T8: stateCtx has content');

    // Overwrite and restore
    Module.vm.run('state_var = "gone"', 1000);
    Module.gc.restoreCheckpoint(cp);
    const r = Module.vm.run('print(state_var)', 1000);
    assert(r.stdout.trim() === 'hello', 'T8: restoreCheckpoint recovers state');

    Module._bc.close();
}

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
