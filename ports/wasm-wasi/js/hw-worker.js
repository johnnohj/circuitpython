/**
 * hw-worker.js — Web Worker that runs the hardware worker WASM instance.
 *
 * Once started, the WASM blocks this Worker's JS event loop — onmessage
 * will never fire while C is executing.  ALL communication with the
 * main thread goes through OPFS:
 *
 *   Main thread writes:                 Worker reads via WASI VFS:
 *     /hw/control        (signals)        open("/hw/control", O_RDONLY)
 *     /hw/repl/rx        (keyboard)       open("/hw/repl/rx", O_RDONLY)
 *     /hw/events/resize  (canvas size)    open("/hw/events/resize", O_RDONLY)
 *
 *   Worker writes:                      Main thread reads via async OPFS:
 *     /hw/display/fb     (framebuffer)    getFileHandle('fb').getFile()
 *     /hw/control        (state/ticks)    getFileHandle('control').getFile()
 *
 * The only postMessage is { type: 'ready' } before WASM starts, and
 * { type: 'exit' } / { type: 'error' } after it finishes.  stdout/stderr
 * are captured via WASI fd_write hooks during execution.
 */

import { OpfsWasi } from './opfs-wasi.js';

async function ensureHWDirs(root) {
    const dirs = [
        'hw',
        'hw/display',
        'hw/repl',
        'hw/gpio',
        'hw/neopixel',
        'hw/i2c',
        'hw/spi',
        'hw/events',
        'lib',
    ];
    for (const dir of dirs) {
        const parts = dir.split('/');
        let handle = root;
        for (const part of parts) {
            handle = await handle.getDirectoryHandle(part, { create: true });
        }
    }
}

async function initAndRun(wasmUrl) {
    let wasi = null;
    try {
        const root = await navigator.storage.getDirectory();

        // Ensure /hw/ directory tree exists in OPFS
        await ensureHWDirs(root);

        // Create WASI runtime
        wasi = new OpfsWasi(root, {
            args: ['circuitpython-worker'],
        });

        // stdout/stderr: capture via WASI fd_write hooks
        // These fire synchronously during WASM execution (not postMessage)
        const stdoutChunks = [];
        const stderrChunks = [];

        wasi.onStdout = (text) => { stdoutChunks.push(text); };
        wasi.onStderr = (text) => { stderrChunks.push(text); };

        // Pre-open directories for synchronous OPFS access
        const hwDir = await root.getDirectoryHandle('hw', { create: true });
        const libDir = await root.getDirectoryHandle('lib', { create: true });

        await wasi.preopenDir('/hw', hwDir);
        await wasi.preopenDir('/lib', libDir);

        // Compile and instantiate worker WASM binary
        let wasmModule;
        try {
            wasmModule = await WebAssembly.compileStreaming(fetch(wasmUrl));
        } catch (e) {
            const response = await fetch(wasmUrl);
            const bytes = await response.arrayBuffer();
            wasmModule = await WebAssembly.compile(bytes);
        }
        const instance = await WebAssembly.instantiate(
            wasmModule, wasi.getImports()
        );
        wasi.setInstance(instance);

        // Signal ready BEFORE blocking on wasi.start()
        self.postMessage({ type: 'ready' });

        // Run worker main loop.
        // This BLOCKS until SIG_TERM or exit — no JS runs until it returns.
        // The C/Python poll loop inside uses WASI poll_oneoff or nanosleep
        // for its 1ms yield, which the WASI runtime implements as a
        // synchronous OPFS flush point.
        const exitCode = wasi.start();

        // WASM finished — send final output and exit code
        if (stdoutChunks.length) {
            self.postMessage({ type: 'stdout', data: stdoutChunks.join('') });
        }
        if (stderrChunks.length) {
            self.postMessage({ type: 'stderr', data: stderrChunks.join('') });
        }
        self.postMessage({ type: 'exit', code: exitCode });

    } catch (e) {
        self.postMessage({
            type: 'error',
            message: e.message || String(e)
        });
    } finally {
        if (wasi) wasi.close();
    }
}

// The only message we handle is 'init' — everything after that is OPFS.
self.onmessage = async (event) => {
    if (event.data.type === 'init') {
        await initAndRun(event.data.wasmUrl);
    }
};
