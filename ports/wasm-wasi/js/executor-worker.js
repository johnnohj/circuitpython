/**
 * executor-worker.js — Web Worker that runs CircuitPython via WASI
 *
 * Receives messages from the main thread:
 *   { type: 'init', wasmUrl: '...', code: '...' }
 *   { type: 'stdin', data: '...' }
 *
 * Sends messages to the main thread:
 *   { type: 'stdout', data: '...' }
 *   { type: 'stderr', data: '...' }
 *   { type: 'exit', code: N }
 *   { type: 'error', message: '...' }
 */

import { OpfsWasi } from './opfs-wasi.js';

let wasi = null;

async function ensureOPFS() {
    const root = await navigator.storage.getDirectory();

    // Create directory structure
    const dirs = ['state', 'dev', 'circuitpy', 'circuitpy/lib'];
    for (const dir of dirs) {
        const parts = dir.split('/');
        let handle = root;
        for (const part of parts) {
            handle = await handle.getDirectoryHandle(part, { create: true });
        }
    }

    return root;
}

async function writeSourceToOPFS(root, code) {
    const circuitpy = await root.getDirectoryHandle('circuitpy', { create: true });
    const codeFile = await circuitpy.getFileHandle('code.py', { create: true });
    const writable = await codeFile.createWritable();
    await writable.write(code);
    await writable.close();
}

async function initAndRun(wasmUrl, code) {
    try {
        const root = await ensureOPFS();

        // Write code.py to OPFS
        await writeSourceToOPFS(root, code);

        // Create WASI runtime
        wasi = new OpfsWasi(root, {
            args: ['circuitpython', '/circuitpy/code.py', '/state'],
        });

        // Hook stdout/stderr to post messages
        wasi.onStdout = (text) => {
            self.postMessage({ type: 'stdout', data: text });
        };
        wasi.onStderr = (text) => {
            self.postMessage({ type: 'stderr', data: text });
        };

        // Pre-open the directory trees for synchronous access
        const stateDir = await root.getDirectoryHandle('state', { create: true });
        const devDir = await root.getDirectoryHandle('dev', { create: true });
        const circuitpyDir = await root.getDirectoryHandle('circuitpy', { create: true });

        await wasi.preopenDir('/state', stateDir);
        await wasi.preopenDir('/dev', devDir);
        await wasi.preopenDir('/circuitpy', circuitpyDir);

        // Compile and instantiate WASM
        const wasmResponse = await fetch(wasmUrl);
        const wasmModule = await WebAssembly.compileStreaming(wasmResponse);
        const instance = await WebAssembly.instantiate(wasmModule, wasi.getImports());
        wasi.setInstance(instance);

        // Run!
        const exitCode = wasi.start();
        self.postMessage({ type: 'exit', code: exitCode });

    } catch (e) {
        self.postMessage({ type: 'error', message: e.message || String(e) });
    } finally {
        if (wasi) wasi.close();
    }
}

// Handle messages from main thread
self.onmessage = async (event) => {
    const msg = event.data;

    if (msg.type === 'init') {
        await initAndRun(msg.wasmUrl, msg.code);
    } else if (msg.type === 'stdin') {
        // TODO: write to /dev/repl stdin ring
    }
};
