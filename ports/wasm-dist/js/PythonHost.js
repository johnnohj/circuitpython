/*
 * PythonHost.js — window.python facade
 *
 * Externally: one Python interpreter with exec(), writeModule(), on().
 * Internally: fleet of Workers on BroadcastChannel('python-dist').
 *
 * Usage:
 *   const python = new PythonHost();
 *   await python.init();
 *   const result = await python.exec('x = 1 + 1');
 *   // result: { delta: { x: 2 }, stdout: '', stderr: '', aborted: false, ... }
 */

import { MSG, CHANNEL } from './BroadcastBus.js';
import { WorkerPool }   from './WorkerPool.js';
import { BLINKA_SHIMS } from './shims.js';

let _requestSeq = 0;
function nextId() { return ++_requestSeq; }

export class PythonHost {
    /**
     * @param {Object} opts
     * @param {number} opts.executors   Executor worker count (default 1)
     * @param {number} opts.compilers   Compiler worker count (default 0)
     * @param {number} opts.timeout     Default exec timeout ms (default 500)
     */
    constructor(opts = {}) {
        this._defaultTimeout = opts.timeout ?? 500;
        this._pool    = new WorkerPool(opts);
        this._pending = new Map();   // id → { resolve, reject, timer }
        this._listeners = {};        // event → Set<callback>
        this._moduleFiles = {};      // path → source, for forwarding to spawned workers

        // Wire pool message routing
        this._pool._onMessage = (msg) => this._route(msg);

        // BroadcastChannel for Python bc_out events (python_stdout, worker_spawn, etc.)
        try {
            this._bc = new BroadcastChannel(CHANNEL);
            this._bc.onmessage = (e) => this._route(e.data);
        } catch {
            // BroadcastChannel may not be available in all environments
            this._bc = null;
        }
    }

    /** Start all workers. Must be awaited before calling exec(). */
    async init() {
        await this._pool.start();
    }

    /**
     * Execute Python code in an executor worker.
     * @param {string} code
     * @param {Object} opts  { timeout?, workerId? }
     * @returns {Promise<{delta, stdout, stderr, aborted, duration_ms, frames}>}
     */
    exec(code, opts = {}) {
        const timeout = opts.timeout ?? this._defaultTimeout;
        const entry   = this._pool.nextExecutor();
        if (!entry) {
            return Promise.reject(new Error('No executor workers available'));
        }

        const id = nextId();
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this._pending.delete(id);
                reject(new Error('exec() JS-level timeout — worker may be stuck'));
            }, timeout + 5000);

            this._pending.set(id, { resolve, reject, timer });
            entry.worker.postMessage({ type: MSG.RUN_REQUEST, id, code, timeout });
        });
    }

    /**
     * Write a Python source file into all executor workers' /flash/.
     * @param {string} path     e.g. 'mymodule.py' or '/flash/mymodule.py'
     * @param {string} source   Python source code
     * @param {string} targetId  Worker ID to target, or null for all
     */
    writeModule(path, source, targetId = null) {
        const full = path.startsWith('/') ? path : '/flash/' + path;
        this._moduleFiles[full] = source;  // track for spawned workers
        const msg  = { type: MSG.MEMFS_UPDATE, path: full, content: source };
        const targets = targetId
            ? this._pool._executors.filter(e => e.workerId === targetId)
            : this._pool._executors;
        targets.forEach(e => e.worker.postMessage(msg));
    }

    /**
     * Compile a Python module to .mpy bytecode via the compiler worker.
     * On success, the .mpy bytes are written to all executor workers' /flash/.
     * @param {string} name    Module name (no extension), e.g. 'mymod'
     * @param {string} source  Python source code
     * @returns {Promise<{path: string, pyPath: string, error?: string}>}
     *          path = /flash/<name>.mpy on success, /flash/<name>.py on compile error
     */
    compile(name, source) {
        const compiler = this._pool.nextCompiler();
        if (!compiler) {
            return Promise.reject(new Error('No compiler workers available'));
        }

        const id = nextId();
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this._pending.delete(id);
                reject(new Error('compile() JS-level timeout'));
            }, 30000);

            this._pending.set(id, { resolve, reject, timer, type: 'compile' });
            compiler.worker.postMessage({ type: MSG.COMPILE_REQUEST, id, name, source });
        });
    }

    /**
     * Subscribe to a Python event.
     * @param {'stdout'|'stderr'|'exception'|'spawn'} event
     * @param {Function} cb
     */
    on(event, cb) {
        if (!this._listeners[event]) { this._listeners[event] = new Set(); }
        this._listeners[event].add(cb);
        return () => this._listeners[event].delete(cb);  // unsubscribe
    }

    /** Request a KeyboardInterrupt in an executor worker via /dev/interrupt.
     *  This sends a MEMFS_UPDATE that writes 0x03 to /dev/interrupt.
     *  mp_hal_hook() picks it up within 64 bytecodes. */
    interrupt(targetId = null) {
        const msg = { type: MSG.MEMFS_UPDATE, path: '/dev/interrupt', content: '\x03' };
        const targets = targetId
            ? this._pool._executors.filter(e => e.workerId === targetId)
            : this._pool._executors;
        targets.forEach(e => e.worker.postMessage(msg));
    }

    /**
     * Write hardware register state to all executor workers.
     * The values are written to /dev/bc_in so that Python's
     * _blinka.sync_registers() (called in time.sleep()) picks them up.
     *
     * @param {Object} regs  e.g. { LED: 1, D5: 0, A0: 512 }
     */
    writeRegisters(regs) {
        const json = JSON.stringify(regs);
        this._pool._executors.forEach(e => {
            e.worker.postMessage({
                type: MSG.MEMFS_UPDATE,
                path: '/dev/bc_in',
                content: json,
            });
        });
    }

    /**
     * Install all blinka compatibility shims into /flash/lib/ on every executor.
     * After calling this, Python code can do:
     *   from digitalio import DigitalInOut, Direction
     *   import board, neopixel
     */
    installBlinka() {
        for (const [name, source] of Object.entries(BLINKA_SHIMS)) {
            this.writeModule('lib/' + name, source);
        }
    }

    /** Shut down all workers. */
    async shutdown() {
        if (this._bc) { this._bc.close(); }
        await this._pool.shutdown();
        this._pending.forEach(({ reject, timer }) => {
            clearTimeout(timer);
            reject(new Error('PythonHost shutting down'));
        });
        this._pending.clear();
    }

    // ── Internal ──────────────────────────────────────────────────────────────

    _route(msg) {
        switch (msg.type) {
            case MSG.RUN_RESPONSE:    this._onRunResponse(msg);    break;
            case MSG.COMPILE_DONE:    this._onCompileDone(msg);    break;
            case MSG.WORKER_SPAWN:    this._onWorkerSpawn(msg);    break;
            case MSG.PYTHON_STDOUT:   this._emit('stdout', msg.payload); break;
            case MSG.PYTHON_STDERR:   this._emit('stderr', msg.payload); break;
            case MSG.EXCEPTION_EVENT: this._emit('exception', msg.payload); break;
            case MSG.HW_EVENT:        this._emit('hardware', msg);           break;
        }
    }

    _onRunResponse({ id, result, error }) {
        const pending = this._pending.get(id);
        if (!pending) { return; }
        this._pending.delete(id);
        clearTimeout(pending.timer);
        if (error) {
            pending.reject(new Error(error));
        } else {
            // Emit stdout/stderr events if present
            if (result?.stdout) { this._emit('stdout', result.stdout); }
            if (result?.stderr) { this._emit('stderr', result.stderr); }
            pending.resolve(result);
        }
    }

    _onCompileDone({ id, path, pyPath, mpyBytes, error }) {
        const pending = this._pending.get(id);
        if (!pending) { return; }
        this._pending.delete(id);
        clearTimeout(pending.timer);

        // Forward compiled bytes (or source fallback) to all executor workers
        if (mpyBytes) {
            // Zero-copy: mpyBytes is a transferred Uint8Array
            const content = new Uint8Array(mpyBytes.buffer ?? mpyBytes);
            const msg = { type: MSG.MEMFS_UPDATE, path, content };
            this._pool._executors.forEach(e => {
                // Clone the buffer for each worker (can't transfer to multiple)
                const copy = new Uint8Array(content);
                e.worker.postMessage({ type: MSG.MEMFS_UPDATE, path, content: copy });
            });
        }

        pending.resolve({ path, pyPath, error });
    }

    async _onWorkerSpawn(msg) {
        const { payload } = msg;
        if (!payload) { return; }
        const entry = await this._pool.spawnExecutor({ workerId: payload.workerId });
        // Forward all known module files to the spawned worker
        const allFiles = { ...this._moduleFiles, ...(payload.module_files || {}) };
        for (const [path, source] of Object.entries(allFiles)) {
            entry.worker.postMessage({ type: MSG.MEMFS_UPDATE, path, content: source });
        }
        // Send the spawn run request; register a pending entry so stdout/stderr events are emitted
        if (payload.run_code) {
            const id = nextId();
            this._pending.set(id, { resolve: () => {}, reject: () => {}, timer: null });
            entry.worker.postMessage({ type: MSG.RUN_REQUEST, id, code: payload.run_code, timeout: 30000 });
        }
        this._emit('spawn', { workerId: entry.workerId });
    }

    _emit(event, data) {
        const cbs = this._listeners[event];
        if (!cbs) { return; }
        cbs.forEach(cb => { try { cb(data); } catch {} });
    }
}

// Expose as window.python in browser context
if (typeof window !== 'undefined') {
    window.PythonHost = PythonHost;
}
