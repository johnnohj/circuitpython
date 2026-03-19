/*
 * PythonREPL.js — Interactive Python REPL backed by a dedicated WASM worker
 *
 * Usage:
 *   import { PythonREPL } from './PythonREPL.js';
 *   const repl = new PythonREPL();
 *   await repl.init();
 *   repl.on('output', text => terminal.write(text));
 *   repl.on('error',  text => terminal.write(text));
 *   repl.on('prompt', p    => terminal.write(p));
 *   repl.sendLine('x = 1 + 1');
 *   repl.sendLine('print(x)');
 *
 * The REPL worker maintains its own Python VM instance with persistent state
 * between sendLine() calls.  stdout/stderr are isolated from PythonHost.
 */

import { MSG } from './BroadcastBus.js';

// ── Node.js module pre-load (top-level await, only in Node) ─────────────────

const isNode = typeof process !== 'undefined' && !!process.versions?.node;

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

function resolveReplWorkerPath() {
    const relative = './workers/repl.js';
    if (isNode) {
        return _join(_dirname(_fileURLToPath(import.meta.url)), relative);
    }
    return new URL(relative, import.meta.url).href;
}

function spawnReplWorker(scriptPath, workerData = {}) {
    if (isNode) {
        return new _NodeWorker(scriptPath, {
            workerData,
            execArgv: ['--experimental-vm-modules'],
        });
    }
    const w = new Worker(scriptPath, { type: 'module' });
    // Shim .on() to match Node-style API
    w.on = (event, handler) => {
        w.addEventListener(
            event === 'message' ? 'message' : event,
            event === 'message' ? (e) => handler(e.data) : handler,
        );
    };
    return w;
}

// ── PythonREPL ───────────────────────────────────────────────────────────────

export class PythonREPL {
    constructor(opts = {}) {
        this._opts      = opts;
        this._worker    = null;   // { worker, workerId }
        this._listeners = {};     // event → Set<callback>
        this._ready     = false;
    }

    /**
     * Spawn the REPL worker and wait for it to be ready.
     * @returns {Promise<void>}
     */
    async init() {
        const scriptPath = resolveReplWorkerPath();
        const workerId   = 'repl-' + Math.random().toString(36).slice(2, 8);
        const worker     = spawnReplWorker(scriptPath, { workerId, role: 'repl' });

        this._worker = { worker, workerId };

        // Single listener handles both the ready phase and all subsequent messages.
        // Calling worker.on() twice would register two listeners and double-dispatch.
        let _waitingForReady = true;
        let _readyResolve, _readyReject;
        const readyPromise = new Promise((res, rej) => {
            _readyResolve = res;
            _readyReject  = rej;
        });
        const timeout = setTimeout(() =>
            _readyReject(new Error('REPL worker ready timeout')), 15000);

        worker.on('message', (msg) => {
            if (_waitingForReady) {
                if (msg.type === MSG.WORKER_READY && msg.workerId === workerId) {
                    _waitingForReady = false;
                    this._ready = true;
                    clearTimeout(timeout);
                    _readyResolve();
                }
                // Drop other messages during init (e.g. the initial repl_prompt).
                // Callers register their listeners after init() resolves.
                return;
            }
            this._dispatch(msg);
        });

        await readyPromise;
    }

    /**
     * Send a line of input to the REPL.
     * The worker accumulates lines until the block is complete, then runs it.
     * @param {string} line  Text typed by the user (no trailing newline needed)
     */
    sendLine(line) {
        if (!this._ready) { throw new Error('PythonREPL not initialized'); }
        this._worker.worker.postMessage({ type: 'repl_input', line });
    }

    /**
     * Clear the incomplete-block accumulation buffer (Ctrl+C effect).
     */
    reset() {
        if (!this._ready) { return; }
        this._worker.worker.postMessage({ type: 'repl_reset' });
    }

    /**
     * Write a file into the REPL worker's MEMFS (e.g. to make a module importable).
     * @param {string} path     MEMFS path, e.g. '/flash/mymod.py'
     * @param {string|Uint8Array} content
     */
    writeFile(path, content) {
        if (!this._ready) { return; }
        this._worker.worker.postMessage({ type: MSG.MEMFS_UPDATE, path, content });
    }

    /**
     * Subscribe to a REPL event.
     * @param {'output'|'error'|'prompt'} event
     * @param {Function} cb
     * @returns {Function}  Unsubscribe
     */
    on(event, cb) {
        if (!this._listeners[event]) { this._listeners[event] = new Set(); }
        this._listeners[event].add(cb);
        return () => this._listeners[event].delete(cb);
    }

    /** Terminate the REPL worker. */
    async shutdown() {
        if (this._worker) {
            await this._worker.worker.terminate();
            this._worker = null;
            this._ready  = false;
        }
    }

    // ── Internal ──────────────────────────────────────────────────────────────

    _dispatch(msg) {
        switch (msg.type) {
            case 'repl_output': this._emit('output', msg.text);   break;
            case 'repl_error':  this._emit('error',  msg.text);   break;
            case 'repl_prompt': this._emit('prompt', msg.prompt); break;
        }
    }

    _emit(event, data) {
        const cbs = this._listeners[event];
        if (!cbs) { return; }
        cbs.forEach(cb => { try { cb(data); } catch {} });
    }
}
