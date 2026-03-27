/**
 * worker-offscreen.js — Single-VM worker with OffscreenCanvas.
 *
 * The worker owns everything: WASM VM, display, REPL, hardware state.
 * Main thread is just a DOM event forwarder.
 *
 * Lifecycle:
 *   cp_init() → [cp_run_file("/code.py") → cp_vm_step loop] → cp_reset()
 *   → cp_run_repl() → cp_step loop → (Ctrl+D → restart code.py)
 *
 * Main → Worker:
 *   { type: 'init', canvas: OffscreenCanvas, wasmUrl: string }
 *   { type: 'key',  char: int }
 *   { type: 'keys', chars: [int...] }
 *   { type: 'run',  path: string }
 *   { type: 'write_file', path: string, data: ArrayBuffer|string }
 *   { type: 'resize', width, height }
 *   { type: 'stop' }
 *
 * Worker → Main:
 *   { type: 'ready', width, height }
 *   { type: 'hw_diff', data, count }
 *   { type: 'stdout', text, fd }
 *   { type: 'mode', mode: 'code'|'repl' }
 *   { type: 'exit', code }
 *   { type: 'error', message }
 */

import { WasiMemfs, IdbBackend } from './wasi-memfs.js';

let wasi = null;
let wasm = null;
let memory = null;
let running = false;
let canvas = null;
let ctx = null;
let imageData = null;
let fbWidth = 0;
let fbHeight = 0;

// Lifecycle state machine
const MODE_REPL = 'repl';
const MODE_CODE = 'code';
let mode = MODE_REPL;

// ---- String marshaling ----
// cp_run_file takes a C string pointer.  Write the path into a
// scratch area at the end of WASM linear memory.

const encoder = new TextEncoder();

function withCString(str, fn) {
    const memSize = memory.buffer.byteLength;
    const scratchPtr = memSize - 256;
    const encoded = encoder.encode(str + '\0');
    if (encoded.length > 256) throw new Error('String too long for scratch area');
    new Uint8Array(memory.buffer, scratchPtr, encoded.length).set(encoded);
    return fn(scratchPtr);
}

// ---- Display ----

function paintFrame() {
    const fbPtr = wasm.exports.cp_get_fb_ptr();
    if (!fbPtr || !ctx) return;

    const fb = new Uint8Array(memory.buffer, fbPtr, fbWidth * fbHeight * 2);
    const rgba = imageData.data;

    // RGB565 → RGBA8888
    for (let i = 0, j = 0; i < fb.length; i += 2, j += 4) {
        const lo = fb[i], hi = fb[i + 1];
        rgba[j]     = hi & 0xF8;
        rgba[j + 1] = ((hi & 0x07) << 5) | ((lo & 0xE0) >> 3);
        rgba[j + 2] = (lo & 0x1F) << 3;
        rgba[j + 3] = 255;
    }

    ctx.putImageData(imageData, 0, 0);
}

function emitHwState() {
    const diffCount = wasm.exports.worker_u2if_build_diff();
    if (diffCount > 0) {
        const diffPtr = wasm.exports.worker_u2if_diff_ptr();
        const diffData = new Uint8Array(diffCount * 64);
        diffData.set(new Uint8Array(memory.buffer, diffPtr, diffCount * 64));
        self.postMessage(
            { type: 'hw_diff', data: diffData.buffer, count: diffCount },
            [diffData.buffer]
        );
    }
}

// ---- Step loops ----

function replStepLoop() {
    if (!running || mode !== MODE_REPL) return;

    const status = wasm.exports.cp_step();

    if (status & 0x01) paintFrame();
    emitHwState();

    if (status & 0x02) {
        // Ctrl+D in REPL — try to run code.py
        runCodePy();
        return;
    }

    setTimeout(replStepLoop, 0);
}

function codeStepLoop() {
    if (!running || mode !== MODE_CODE) return;

    // Check if a yield delay is still active
    if (wasm.exports.cp_delay_active()) {
        setTimeout(codeStepLoop, 1);
        return;
    }

    const result = wasm.exports.cp_vm_step();

    // Refresh terminal display (composites dirty text into framebuffer)
    const displayUpdated = wasm.exports.cp_refresh_display();
    if (displayUpdated) paintFrame();
    emitHwState();

    if (result === 1) {
        // Yielded — check reason and schedule next step
        const reason = wasm.exports.cp_get_yield_reason();
        const arg = wasm.exports.cp_get_yield_arg();

        switch (reason) {
            case 1: // YIELD_SLEEP
                setTimeout(codeStepLoop, arg);
                break;
            case 2: // YIELD_SHOW
                setTimeout(codeStepLoop, 0);
                break;
            default: // YIELD_BUDGET or YIELD_IO_WAIT
                setTimeout(codeStepLoop, 0);
                break;
        }
    } else {
        // result 0 (done) or 2 (exception) — transition to REPL
        switchToRepl();
    }
}

// ---- Lifecycle transitions ----

function runCodePy(path = '/code.py') {
    // Clean up previous REPL/code state
    wasm.exports.cp_reset();

    const ret = withCString(path, (ptr) => wasm.exports.cp_run_file(ptr));
    if (ret !== 0) {
        // No code.py or compile error — go straight to REPL
        switchToRepl();
        return;
    }
    mode = MODE_CODE;
    self.postMessage({ type: 'mode', mode: 'code' });
    codeStepLoop();
}

function switchToRepl() {
    wasm.exports.cp_reset();
    wasm.exports.cp_run_repl();
    mode = MODE_REPL;
    self.postMessage({ type: 'mode', mode: 'repl' });
    replStepLoop();
}

// ---- Init ----

async function init(offscreenCanvas, wasmUrl) {
    try {
        canvas = offscreenCanvas;
        ctx = canvas.getContext('2d');

        // Set up WASI filesystem with IndexedDB persistence
        const idb = new IdbBackend({ prefix: '/' });
        wasi = new WasiMemfs({
            args: ['circuitpython'],
            idb,
            onStdout: (text) => self.postMessage({ type: 'stdout', text, fd: 1 }),
            onStderr: (text) => self.postMessage({ type: 'stdout', text, fd: 2 }),
        });

        // Restore persisted files from IndexedDB
        const restoredCount = await idb.load(wasi);
        console.log(`[cp] restored ${restoredCount} files from IndexedDB`);

        // Ensure essential directories exist
        wasi.dirs.add('/lib');
        wasi.dirs.add('/circuitpy');

        console.log('[cp] fetching WASM from:', wasmUrl);
        let wasmModule;
        try {
            wasmModule = await WebAssembly.compileStreaming(fetch(wasmUrl));
        } catch (e) {
            console.warn('[cp] compileStreaming failed:', e.message);
            const response = await fetch(wasmUrl);
            if (!response.ok) throw new Error(`Fetch failed: ${response.status}`);
            wasmModule = await WebAssembly.compile(await response.arrayBuffer());
        }

        const imports = wasi.getImports();

        // Override proc_exit — don't throw, just stop the step loop
        imports.wasi_snapshot_preview1.proc_exit = (code) => {
            running = false;
            self.postMessage({ type: 'exit', code });
        };

        const instance = await WebAssembly.instantiate(wasmModule, imports);
        wasm = instance;
        memory = instance.exports.memory;
        wasi.setInstance(instance);

        // Init the VM
        wasm.exports.cp_init();

        fbWidth = wasm.exports.cp_get_fb_width();
        fbHeight = wasm.exports.cp_get_fb_height();

        // Size the OffscreenCanvas to match framebuffer
        canvas.width = fbWidth;
        canvas.height = fbHeight;
        imageData = ctx.createImageData(fbWidth, fbHeight);

        self.postMessage({ type: 'ready', width: fbWidth, height: fbHeight });

        // Paint initial frame (REPL prompt)
        paintFrame();

        running = true;

        // Try code.py first, fall back to REPL
        if (wasi.files.has('/code.py')) {
            runCodePy();
        } else {
            mode = MODE_REPL;
            self.postMessage({ type: 'mode', mode: 'repl' });
            replStepLoop();
        }

    } catch (e) {
        self.postMessage({ type: 'error', message: e.message || String(e) });
    }
}

self.onmessage = (event) => {
    const msg = event.data;

    switch (msg.type) {
        case 'init':
            init(msg.canvas, msg.wasmUrl);
            break;

        case 'key':
            if (wasm) wasm.exports.cp_push_key(msg.char);
            break;

        case 'keys':
            if (wasm) {
                for (const c of msg.chars) {
                    wasm.exports.cp_push_key(c);
                }
            }
            break;

        case 'run':
            if (wasm && msg.path) {
                runCodePy(msg.path);
            }
            break;

        case 'write_file':
            // Write a file to the in-memory filesystem
            // (e.g., main thread sends code.py content before 'run')
            if (wasi && msg.path) {
                wasi.writeFile(msg.path, msg.data);
                console.log(`[cp] wrote ${msg.path} (${typeof msg.data === 'string' ? msg.data.length : msg.data.byteLength} bytes)`);
            }
            break;

        case 'resize':
            if (canvas && msg.width && msg.height) {
                // For now we keep the framebuffer fixed; just update CSS scaling
            }
            break;

        case 'stop':
            running = false;
            self.postMessage({ type: 'exit', code: 0 });
            break;
    }
};
