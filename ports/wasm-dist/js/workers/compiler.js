/*
 * compiler.js — Python compiler WebWorker
 *
 * Compiles Python source to .mpy bytecode via mp_js_compile_to_mpy().
 * Also writes the .py source alongside for debugging/fallback.
 *
 * Messages:
 *   compile_request { id, name, source }  → compile_done { id, path, pyPath }
 *   memfs_update    { path, content }     → write to MEMFS
 */

import { MSG } from '../BroadcastBus.js';

const isNode = typeof process !== 'undefined' && !!process.versions?.node;
let parentPort, workerData;
if (isNode) {
    const wt = await import('worker_threads');
    parentPort = wt.parentPort;
    workerData = wt.workerData || {};
} else {
    parentPort = {
        postMessage: (msg) => self.postMessage(msg),
        on: (_, handler) => { self.onmessage = (e) => handler(e.data); },
    };
    workerData = {};
}

let _fileURLToPath2, _dirname2, _join2;
if (isNode) {
    const url  = await import('url');
    const path = await import('path');
    _fileURLToPath2 = url.fileURLToPath;
    _dirname2       = path.dirname;
    _join2          = path.join;
}
function resolveFromHere(relative) {
    if (isNode) {
        return 'file://' + _join2(_dirname2(_fileURLToPath2(import.meta.url)), relative);
    }
    return relative;
}

const wasmUrl = resolveFromHere('../../build-dist/circuitpython.mjs');
const apiUrl  = resolveFromHere('../../api.js');

const workerId = workerData.workerId || 'comp-' + Math.random().toString(36).slice(2, 8);

const { default: createModule } = await import(wasmUrl);
const { initializeModuleAPI }   = await import(apiUrl);
const Module = await createModule({ _workerId: workerId, _workerRole: 'compiler' });
initializeModuleAPI(Module);
Module.vm.init({ pystackSize: 2 * 1024, heapSize: 256 * 1024 });

parentPort.postMessage({ type: MSG.WORKER_READY, workerId, role: 'compiler' });

parentPort.on('message', (msg) => {
    switch (msg.type) {
        case MSG.COMPILE_REQUEST: handleCompile(msg); break;
        case MSG.MEMFS_UPDATE:
            try { Module.fs.write(msg.path, msg.content); } catch {}
            break;
    }
});

function handleCompile({ id, name, source }) {
    const pyPath  = '/flash/' + name + '.py';
    try {
        // Always write the .py source (debug reference + fallback)
        Module.fs.write(pyPath, source);

        // Compile to .mpy; returns MEMFS path on success, null on error
        const mpyPath = Module.vm.compileToMpy(name, source);
        if (mpyPath !== null) {
            // Read compiled bytes so PythonHost can forward them to executor workers
            const mpyBytes = Module.FS.readFile(mpyPath);  // Uint8Array
            parentPort.postMessage(
                { type: MSG.COMPILE_DONE, id, workerId, path: mpyPath, pyPath, mpyBytes },
                [mpyBytes.buffer]);  // transfer ownership — zero-copy
        } else {
            // Compilation error — stderr already written to /dev/py_stderr.
            // Fall back to .py so the executor can still import by source.
            const stderr = Module.dev.read('py_stderr');
            parentPort.postMessage({ type: MSG.COMPILE_DONE, id, workerId,
                path: pyPath, pyPath, error: stderr || 'compile error' });
        }
    } catch (e) {
        parentPort.postMessage({ type: MSG.COMPILE_DONE, id, workerId, path: null, pyPath, error: String(e) });
    }
}
