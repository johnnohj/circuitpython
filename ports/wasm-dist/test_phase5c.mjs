/*
 * test_phase5c.mjs — Phase 5c: /circuitpy/ filesystem tests
 *
 * Tests:
 *   T1: /flash/circuitpy exists in MEMFS after VM init
 *   T2: /circuitpy is on Python's sys.path
 *   T3: Write module to /flash/circuitpy, import from Python via 'import foo'
 *   T4: PythonHost.writeModule('/circuitpy/...') sends file to /flash/circuitpy
 *   T5: Module.fs.mountIDBFS() returns a Promise (resolves in Node.js — IDBFS no-op)
 *   T6: Module.fs.syncfs(true/false) returns a Promise (no-op in Node.js)
 */

import('./build-dist/circuitpython.mjs').then(async ({ default: createModule }) => {
    const { initializeModuleAPI } = await import('./api.js');
    const { PythonHost }          = await import('./js/PythonHost.js');

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

    // ── Standalone Module tests (T1–T3, T5–T6) ──────────────────────────────

    const Module = await createModule({ _workerId: 'test-5c', _workerRole: 'test' });
    initializeModuleAPI(Module);
    Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

    // T1: /flash/circuitpy directory exists
    {
        let exists = false;
        try {
            const stat = Module.FS.stat('/flash/circuitpy');
            exists = Module.FS.isDir(stat.mode);
        } catch {}
        assert(exists, 'T1: /flash/circuitpy exists in MEMFS');
    }

    // T2: /circuitpy is on Python sys.path
    {
        const r = Module.vm.run('import sys; print("/circuitpy" in sys.path)', 500);
        assert(r.stdout.trim() === 'True', 'T2: /circuitpy is on sys.path');
    }

    // T3: Write a module to /flash/circuitpy, import it from Python
    {
        Module.FS.writeFile('/flash/circuitpy/greet.py', 'def hi(): return "hello from circuitpy"');
        const r = Module.vm.run('import greet; print(greet.hi())', 500);
        assert(r.stdout.trim() === 'hello from circuitpy', 'T3: import from /circuitpy');
        assert(r.stderr === '', 'T3: no import error');
    }

    // T5: mountIDBFS() returns a Promise that resolves (no-op in Node.js)
    {
        let ok = false;
        try {
            const p = Module.fs.mountIDBFS();
            assert(p instanceof Promise, 'T5: mountIDBFS returns a Promise');
            await p;
            ok = true;
        } catch (e) {
            console.error('T5 error:', e);
        }
        assert(ok, 'T5: mountIDBFS Promise resolves');
    }

    // T6: syncfs(true) and syncfs(false) return resolving Promises
    {
        let okSave = false;
        let okLoad = false;
        try {
            await Module.fs.syncfs(false);
            okLoad = true;
        } catch {}
        try {
            await Module.fs.syncfs(true);
            okSave = true;
        } catch {}
        assert(okLoad, 'T6: syncfs(false) resolves');
        assert(okSave, 'T6: syncfs(true) resolves');
    }

    // ── PythonHost integration (T4) ──────────────────────────────────────────

    const python = new PythonHost({ executors: 1 });
    await python.init();

    // T4: PythonHost.writeModule with MEMFS path /flash/circuitpy puts file importable as /circuitpy
    {
        python.writeModule('/flash/circuitpy/utils.py', 'VALUE = 99');
        // Give the worker a moment to process the MEMFS_UPDATE
        await new Promise(r => setTimeout(r, 100));
        const r = await python.exec('import utils; print(utils.VALUE)');
        assert(r.stdout.trim() === '99', 'T4: module in /circuitpy importable via PythonHost');
        assert(!r.stderr, 'T4: no error');
    }

    await python.shutdown();

    console.log(`\n${passed} passed, ${failed} failed`);
    process.exit(failed > 0 ? 1 : 0);
});
