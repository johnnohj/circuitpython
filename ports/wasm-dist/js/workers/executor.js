/*
 * executor.js — Python executor WebWorker
 *
 * Loads the WASM Python VM, initializes it, and handles:
 *   run_request   { id, code, timeout }    → run_response { id, result }
 *   memfs_update  { path, content }        → write to MEMFS
 *
 * Python bc_out events (python_stdout, python_stderr, worker_spawn) are
 * broadcast on BroadcastChannel('python-dist') from mp_js_hook().
 *
 * Compatible with both browser WebWorkers and Node.js worker_threads.
 */

import { MSG, CHANNEL } from '../BroadcastBus.js';
import { VDevBridge } from '../VDevBridge.js';

// ── Environment detection ───────────────────────────────────────────────────

const isNode = typeof process !== 'undefined' && !!process.versions?.node;

let parentPort, workerData;
if (isNode) {
    const wt = await import('worker_threads');
    parentPort = wt.parentPort;
    workerData = wt.workerData || {};
} else {
    // Browser WebWorker: self is the global scope
    parentPort = {
        postMessage: (msg) => self.postMessage(msg),
        on: (_, handler) => { self.onmessage = (e) => handler(e.data); },
    };
    workerData = {};
}

// ── WASM module path ────────────────────────────────────────────────────────

let _fileURLToPath, _dirname, _join;
if (isNode) {
    const url  = await import('url');
    const path = await import('path');
    _fileURLToPath = url.fileURLToPath;
    _dirname       = path.dirname;
    _join          = path.join;
}

function resolveFromHere(relative) {
    if (isNode) {
        return 'file://' + _join(_dirname(_fileURLToPath(import.meta.url)), relative);
    }
    return relative;
}

const wasmUrl   = resolveFromHere('../../build-dist/circuitpython.mjs');
const apiUrl    = resolveFromHere('../../api.js');

// ── Load WASM + API ────────────────────────────────────────────────────────

const workerId = workerData.workerId || 'exec-' + Math.random().toString(36).slice(2, 8);
const role     = workerData.role    || 'executor';

const { default: createModule } = await import(wasmUrl);
const { initializeModuleAPI }   = await import(apiUrl);

const Module = await createModule({
    _workerId:   workerId,
    _workerRole: role,
});

// BroadcastChannel: Python bc_out events are posted here by mp_js_hook()
Module._bc = new BroadcastChannel(CHANNEL);

initializeModuleAPI(Module);
Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

const bridge = new VDevBridge(Module);

// ── Ready ───────────────────────────────────────────────────────────────────

parentPort.postMessage({ type: MSG.WORKER_READY, workerId, role });

// ── Message dispatch ─────────────────────────────────────────────────────────

parentPort.on('message', (msg) => {
    switch (msg.type) {
        case MSG.RUN_REQUEST:    handleRun(msg);        break;
        case MSG.MEMFS_UPDATE:   handleMemfsUpdate(msg); break;
        case MSG.WORKER_INIT:    handleInit(msg);        break;
        default:
            console.warn('executor:', workerId, 'unknown message:', msg.type);
    }
});

// ── Handlers ────────────────────────────────────────────────────────────────

function handleRun({ id, code, timeout = 500 }) {
    let result;
    try {
        result = Module.vm.run(code, timeout);
    } catch (err) {
        result = {
            delta: {}, stdout: '', stderr: String(err),
            aborted: false, duration_ms: 0, frames: [],
        };
    }
    parentPort.postMessage({ type: MSG.RUN_RESPONSE, id, workerId, result });
}

function handleMemfsUpdate({ path, content }) {
    try {
        bridge.writeFile(path, content);
    } catch (e) {
        console.error('executor: memfs_update failed', path, e);
    }
}

function handleInit({ workerId: newId, role: newRole } = {}) {
    // Future: re-initialize with different identity or heap size
    // For now: acknowledge
    parentPort.postMessage({ type: MSG.WORKER_READY, workerId, role });
}
