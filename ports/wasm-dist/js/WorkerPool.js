/*
 * WorkerPool.js — Worker lifecycle management
 *
 * Creates and manages executor + compiler workers.
 * Supports both browser WebWorker and Node.js worker_threads.
 */

import { MSG, CHANNEL } from './BroadcastBus.js';

const isNode = typeof process !== 'undefined' && !!process.versions?.node;

// Pre-load Node.js modules (top-level await, only runs in Node.js)
let _NodeWorker, _fileURLToPath, _dirname, _join;
if (isNode) {
    const wt   = await import('worker_threads');
    const url  = await import('url');
    const path = await import('path');
    _NodeWorker    = wt.Worker;
    _fileURLToPath = url.fileURLToPath;
    _dirname       = path.dirname;
    _join          = path.join;
}

function resolveWorkerPath(relative) {
    if (isNode) {
        return _join(_dirname(_fileURLToPath(import.meta.url)), relative);
    }
    // Browser: resolve absolute URL relative to this module so Worker() works
    // regardless of the page's base URL.
    return new URL(relative, import.meta.url).href;
}

function spawnWorker(scriptPath, workerData = {}) {
    if (isNode) {
        return new _NodeWorker(scriptPath, { workerData, execArgv: ['--experimental-vm-modules'] });
    }
    const w = new Worker(scriptPath, { type: 'module' });
    // Shim Node-style .on('message', cb) so the rest of WorkerPool can use
    // a single API in both environments.
    w.on = (event, handler) => {
        w.addEventListener(event === 'message' ? 'message' : event,
            event === 'message' ? (e) => handler(e.data) : handler);
    };
    return w;
}

export class WorkerPool {
    /**
     * @param {Object} opts
     * @param {number} opts.executors   Number of executor workers (default 1)
     * @param {number} opts.compilers   Number of compiler workers (default 0)
     */
    constructor(opts = {}) {
        const { executors = 1, compilers = 0 } = opts;
        this._executorCount  = executors;
        this._compilerCount  = compilers;
        this._executors      = [];  // { worker, workerId, ready, busy }
        this._compilers      = [];
        this._onMessage      = null;  // set by PythonHost
        this._nextExecutorIdx = 0;
    }

    /**
     * Start all workers and wait for them to signal WORKER_READY.
     * @returns {Promise<void>}
     */
    start() {
        const executorScript = resolveWorkerPath('./workers/executor.js');
        const compilerScript = resolveWorkerPath('./workers/compiler.js');

        const promises = [];

        for (let i = 0; i < this._executorCount; i++) {
            const id = 'exec-' + i;
            const worker = spawnWorker(executorScript, { workerId: id, role: 'executor' });
            const entry = { worker, workerId: id, ready: false, busy: false };
            this._executors.push(entry);
            promises.push(this._awaitReady(worker, entry));
            worker.on('message', (msg) => this._route(msg, entry));
        }

        for (let i = 0; i < this._compilerCount; i++) {
            const id = 'comp-' + i;
            const worker = spawnWorker(compilerScript, { workerId: id, role: 'compiler' });
            const entry = { worker, workerId: id, ready: false, busy: false };
            this._compilers.push(entry);
            promises.push(this._awaitReady(worker, entry));
            worker.on('message', (msg) => this._route(msg, entry));
        }

        return Promise.all(promises);
    }

    /** Route incoming worker messages to PythonHost._onMessage handler. */
    _route(msg, _entry) {
        if (this._onMessage) { this._onMessage(msg); }
    }

    /** Wait for a WORKER_READY message from a worker. */
    _awaitReady(worker, entry) {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => reject(new Error('Worker ready timeout: ' + entry.workerId)), 15000);
            const originalRoute = this._onMessage;
            const onMsg = (msg) => {
                if (msg.type === MSG.WORKER_READY && msg.workerId === entry.workerId) {
                    entry.ready = true;
                    clearTimeout(timeout);
                    // Restore; don't remove permanently since _onMessage may change
                    resolve();
                }
            };
            worker.on('message', onMsg);
        });
    }

    /**
     * Get the next available executor worker (round-robin).
     * Returns the worker entry or null if none ready.
     */
    nextExecutor() {
        const n = this._executors.length;
        for (let i = 0; i < n; i++) {
            const idx = (this._nextExecutorIdx + i) % n;
            const entry = this._executors[idx];
            if (entry.ready) {
                this._nextExecutorIdx = (idx + 1) % n;
                return entry;
            }
        }
        return this._executors[0] || null;  // fallback
    }

    /** Get the first available compiler worker. */
    nextCompiler() {
        return this._compilers.find(e => e.ready) || null;
    }

    /**
     * Spawn an additional executor worker at runtime.
     * Used by PythonHost to handle _thread.start_new_thread() requests.
     * @returns {Promise<entry>}
     */
    async spawnExecutor(workerData = {}) {
        const id = workerData.workerId || 'exec-dyn-' + Math.random().toString(36).slice(2, 8);
        const script = resolveWorkerPath('./workers/executor.js');
        const worker = spawnWorker(script, { workerId: id, role: 'executor', ...workerData });
        const entry  = { worker, workerId: id, ready: false, busy: false };
        this._executors.push(entry);
        worker.on('message', (msg) => this._route(msg, entry));
        await this._awaitReady(worker, entry);
        return entry;
    }

    /** Terminate all workers cleanly. */
    async shutdown() {
        const all = [...this._executors, ...this._compilers];
        await Promise.all(all.map(e => e.worker.terminate()));
        this._executors = [];
        this._compilers = [];
    }
}
