/*
 * repl.js — Python REPL WebWorker
 *
 * Owns a single WASM VM instance in "repl" role.  Accepts lines of input one
 * at a time, accumulates them until mp_repl_continue_with_input() says the
 * block is complete, then executes the block via Module.vm.run() and streams
 * stdout/stderr back to PythonREPL.
 *
 * Messages IN  (from PythonREPL):
 *   { type: 'repl_input',   line: string }
 *   { type: 'repl_reset' }              ← clear accumulation buffer
 *   { type: 'memfs_update', path, content }
 *
 * Messages OUT (to PythonREPL):
 *   { type: 'worker_ready', workerId, role: 'repl' }
 *   { type: 'repl_output',  workerId, text }        ← stdout + stderr combined
 *   { type: 'repl_prompt',  workerId, prompt }      ← '>>> ' or '... '
 *   { type: 'repl_error',   workerId, text }        ← execution error text
 */

import { MSG } from '../BroadcastBus.js';
import { VDevBridge } from '../VDevBridge.js';

// ── Environment detection ───────────────────────────────────────────────────

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

const wasmUrl = resolveFromHere('../../build-dist/circuitpython.mjs');
const apiUrl  = resolveFromHere('../../api.js');

// ── Load WASM + API ─────────────────────────────────────────────────────────

const workerId = workerData.workerId || 'repl-' + Math.random().toString(36).slice(2, 8);

const { default: createModule } = await import(wasmUrl);
const { initializeModuleAPI }   = await import(apiUrl);

const Module = await createModule({
    _workerId:   workerId,
    _workerRole: 'repl',
});

initializeModuleAPI(Module);
Module.vm.init({ pystackSize: 4 * 1024, heapSize: 512 * 1024 });

const bridge = new VDevBridge(Module);

// ── REPL accumulation state ─────────────────────────────────────────────────

let _lines = [];    // accumulated lines for the current block
const TIMEOUT_MS = 10000;  // generous timeout per execution

function resetBuffer() {
    _lines = [];
}

function currentPrompt() {
    return _lines.length === 0 ? '>>> ' : '... ';
}

// ── Ready ───────────────────────────────────────────────────────────────────

parentPort.postMessage({ type: MSG.WORKER_READY, workerId, role: 'repl' });
// No initial prompt: callers know REPL starts with '>>> '.
// Sending it here would race with sendAndWait's prompt-based completion detection.

// ── Message dispatch ─────────────────────────────────────────────────────────

parentPort.on('message', (msg) => {
    switch (msg.type) {
        case 'repl_input':    handleInput(msg.line ?? ''); break;
        case 'repl_reset':    handleReset();               break;
        case MSG.MEMFS_UPDATE: handleMemfsUpdate(msg);     break;
        default:
            console.warn('repl worker: unknown message:', msg.type);
    }
});

// ── Handlers ────────────────────────────────────────────────────────────────

function handleInput(line) {
    _lines.push(line);

    // Ask MicroPython: does the accumulated block need more input?
    // mp_repl_continue_with_input() checks the FULL accumulated text, not just the last line.
    // A trailing newline (empty line appended) signals the end of an indented block.
    const accumulated = _lines.join('\n');
    const needsMore = Module.vm.replContinue(accumulated);

    if (needsMore) {
        // Block is incomplete — ask for another line
        parentPort.postMessage({ type: 'repl_prompt', workerId, prompt: '... ' });
        return;
    }

    // Block is complete — execute it
    const code = _lines.join('\n');
    resetBuffer();

    // Clear output devices before run
    Module.FS.writeFile('/dev/py_stdout', '');
    Module.FS.writeFile('/dev/py_stderr', '');

    let result;
    try {
        result = Module.vm.run(code, TIMEOUT_MS);
    } catch (err) {
        result = {
            stdout: '', stderr: String(err),
            aborted: false, duration_ms: 0, delta: {}, frames: [],
        };
    }

    if (result.stdout) {
        parentPort.postMessage({ type: 'repl_output', workerId, text: result.stdout });
    }
    if (result.stderr) {
        parentPort.postMessage({ type: 'repl_error',  workerId, text: result.stderr });
    }
    if (result.aborted) {
        parentPort.postMessage({ type: 'repl_error',  workerId, text: 'KeyboardInterrupt\n' });
    }

    parentPort.postMessage({ type: 'repl_prompt', workerId, prompt: '>>> ' });
}

function handleReset() {
    resetBuffer();
    parentPort.postMessage({ type: 'repl_prompt', workerId, prompt: '>>> ' });
}

function handleMemfsUpdate({ path, content }) {
    try {
        bridge.writeFile(path, content);
    } catch (e) {
        console.error('repl worker: memfs_update failed', path, e);
    }
}
