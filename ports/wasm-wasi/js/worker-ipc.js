/**
 * worker-ipc.js — postMessage IPC wrapper for the CircuitPython worker.
 *
 * Runs inside a Web Worker.  Uses the reactor-style WASM exports
 * (worker_init / worker_step / worker_push_key / worker_get_fb_*)
 * instead of the blocking _start entry point.
 *
 * JS owns the event loop.  WASM never blocks.  All communication
 * with the main thread goes through postMessage with transferable
 * ArrayBuffers (zero-copy for framebuffers).
 *
 * No CORS headers required.  No OPFS, no SharedArrayBuffer.
 * Works on any static host (GitHub Pages, S3, etc.).
 *
 * Protocol:
 *
 *   Main → Worker:
 *     { type: 'init', wasmUrl: '...' }     Start the VM
 *     { type: 'key',  char: <int> }        Keyboard input (one char)
 *     { type: 'keys', chars: [int...] }    Keyboard input (batch)
 *     { type: 'stop' }                     Shutdown
 *
 *   Worker → Main:
 *     { type: 'ready', width, height }     VM initialized, display size
 *     { type: 'frame', fb: ArrayBuffer }   Framebuffer (transferred)
 *     { type: 'stdout', text: '...' }      Console output
 *     { type: 'exit', code: <int> }        VM stopped
 *     { type: 'error', message: '...' }    Fatal error
 */

let wasm = null;    // WebAssembly.Instance
let memory = null;  // WebAssembly.Memory
let running = false;
let stepInterval = null;

// WASI stub — minimal implementation for reactor mode.
// The reactor exports don't call _start, so most WASI functions
// are unused.  We provide stubs for fd_write (stdout/stderr capture)
// and a few others that the init path needs.
function createWasiStubs() {
    const decoder = new TextDecoder();

    return {
        wasi_snapshot_preview1: {
            // fd_write: capture stdout (fd=1) and stderr (fd=2)
            fd_write(fd, iovs_ptr, iovs_len, nwritten_ptr) {
                const view = new DataView(memory.buffer);
                let totalWritten = 0;
                for (let i = 0; i < iovs_len; i++) {
                    const ptr = view.getUint32(iovs_ptr + i * 8, true);
                    const len = view.getUint32(iovs_ptr + i * 8 + 4, true);
                    const bytes = new Uint8Array(memory.buffer, ptr, len);
                    const text = decoder.decode(bytes);
                    if (fd === 1 || fd === 2) {
                        self.postMessage({ type: 'stdout', text, fd });
                    }
                    totalWritten += len;
                }
                view.setUint32(nwritten_ptr, totalWritten, true);
                return 0; // ESUCCESS
            },

            // fd_read: return EOF (no stdin in reactor mode)
            fd_read(fd, iovs_ptr, iovs_len, nread_ptr) {
                const view = new DataView(memory.buffer);
                view.setUint32(nread_ptr, 0, true);
                return 0;
            },

            // fd_close, fd_seek, fd_fdstat_get: no-ops
            fd_close() { return 0; },
            fd_seek(fd, offset_lo, offset_hi, whence, newoffset_ptr) {
                return 8; // EBADF
            },
            fd_fdstat_get(fd, stat_ptr) {
                const view = new DataView(memory.buffer);
                // filetype = REGULAR_FILE (4), flags = 0
                view.setUint8(stat_ptr, 4);
                view.setUint16(stat_ptr + 2, 0, true);
                // rights: all
                view.setBigUint64(stat_ptr + 8, 0xFFFFFFFFFFFFFFFFn, true);
                view.setBigUint64(stat_ptr + 16, 0xFFFFFFFFFFFFFFFFn, true);
                return 0;
            },
            fd_prestat_get() { return 8; }, // EBADF — no preopens
            fd_prestat_dir_name() { return 8; },

            // Clock
            clock_time_get(id, precision, time_ptr) {
                const view = new DataView(memory.buffer);
                const ns = BigInt(Math.floor(performance.now() * 1e6));
                view.setBigUint64(time_ptr, ns, true);
                return 0;
            },

            // Environment
            environ_sizes_get(count_ptr, size_ptr) {
                const view = new DataView(memory.buffer);
                view.setUint32(count_ptr, 0, true);
                view.setUint32(size_ptr, 0, true);
                return 0;
            },
            environ_get() { return 0; },

            args_sizes_get(argc_ptr, argv_buf_size_ptr) {
                const view = new DataView(memory.buffer);
                view.setUint32(argc_ptr, 0, true);
                view.setUint32(argv_buf_size_ptr, 0, true);
                return 0;
            },
            args_get() { return 0; },

            // Process
            proc_exit(code) {
                running = false;
                self.postMessage({ type: 'exit', code });
            },

            // Filesystem stubs (reactor mode — no real filesystem)
            fd_sync() { return 0; },
            fd_readdir() { return 44; },
            path_open() { return 44; }, // ENOSYS
            path_filestat_get() { return 44; },
            path_create_directory() { return 44; },
            path_remove_directory() { return 44; },
            path_rename() { return 44; },
            path_unlink_file() { return 44; },

            // Misc
            random_get(buf_ptr, buf_len) {
                const buf = new Uint8Array(memory.buffer, buf_ptr, buf_len);
                crypto.getRandomValues(buf);
                return 0;
            },
            poll_oneoff() { return 0; },
        }
    };
}

function sendFrame(w, h) {
    const fbPtr = wasm.exports.worker_get_fb_ptr();
    const fbSize = w * h * 2;
    const fb = new Uint8Array(fbSize);
    fb.set(new Uint8Array(memory.buffer, fbPtr, fbSize));
    self.postMessage(
        { type: 'frame', fb: fb.buffer, width: w, height: h },
        [fb.buffer]
    );
}

async function init(wasmUrl) {
    try {
        const imports = createWasiStubs();

        // Compile and instantiate
        console.log('[worker-ipc] fetching WASM from:', wasmUrl);
        let wasmModule;
        try {
            wasmModule = await WebAssembly.compileStreaming(fetch(wasmUrl));
        } catch (e) {
            console.warn('[worker-ipc] compileStreaming failed:', e.message, '— trying arrayBuffer fallback');
            const response = await fetch(wasmUrl);
            if (!response.ok) {
                throw new Error(`Fetch failed: ${response.status} ${response.statusText} for ${wasmUrl}`);
            }
            const bytes = await response.arrayBuffer();
            console.log('[worker-ipc] fetched', bytes.byteLength, 'bytes');
            wasmModule = await WebAssembly.compile(bytes);
        }

        const instance = await WebAssembly.instantiate(wasmModule, imports);
        wasm = instance;
        memory = instance.exports.memory;

        // Initialize the VM via the reactor export
        wasm.exports.worker_init();

        // Get display dimensions
        const width = wasm.exports.worker_get_fb_width();
        const height = wasm.exports.worker_get_fb_height();

        self.postMessage({ type: 'ready', width, height });

        // Send initial frame (REPL prompt rendered during worker_init)
        sendFrame(width, height);

        // Start the step loop
        running = true;
        stepLoop();

    } catch (e) {
        self.postMessage({ type: 'error', message: e.message || String(e) });
    }
}

function stepLoop() {
    if (!running) return;

    const status = wasm.exports.worker_step();

    // If display was updated, transfer the framebuffer
    if (status & 0x01) { // WORKER_STEP_DISPLAY
        const w = wasm.exports.worker_get_fb_width();
        const h = wasm.exports.worker_get_fb_height();
        sendFrame(w, h);
    }

    // Check for hardware state changes — emit U2IF diff packets
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

    if (status & 0x02) { // WORKER_STEP_EXIT
        running = false;
        self.postMessage({ type: 'exit', code: 0 });
        return;
    }

    // Yield to the browser event loop, then step again.
    // setTimeout(0) lets onmessage handlers run between steps.
    setTimeout(stepLoop, 0);
}

// Message handler — runs between steps (thanks to setTimeout yield)
self.onmessage = (event) => {
    const msg = event.data;

    switch (msg.type) {
        case 'init':
            init(msg.wasmUrl);
            break;

        case 'key':
            if (wasm) wasm.exports.worker_push_key(msg.char);
            break;

        case 'keys':
            if (wasm) {
                for (const c of msg.chars) {
                    wasm.exports.worker_push_key(c);
                }
            }
            break;

        case 'u2if':
            if (wasm) {
                // Write the U2IF packet into WASM linear memory
                const packetPtr = wasm.exports.worker_u2if_packet_ptr();
                const packet = new Uint8Array(memory.buffer, packetPtr, 64);
                const incoming = new Uint8Array(msg.data);
                packet.set(incoming);

                // Dispatch
                wasm.exports.worker_u2if_exec();

                // Read response and send back
                const respPtr = wasm.exports.worker_u2if_response_ptr();
                const resp = new Uint8Array(64);
                resp.set(new Uint8Array(memory.buffer, respPtr, 64));
                self.postMessage(
                    { type: 'u2if_response', data: resp.buffer },
                    [resp.buffer]
                );
            }
            break;

        case 'stop':
            running = false;
            self.postMessage({ type: 'exit', code: 0 });
            break;
    }
};
