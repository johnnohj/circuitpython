/**
 * main-ipc.js — Main thread host for CircuitPython worker.
 *
 * Creates a Web Worker running worker-ipc.js, sends keyboard input
 * via postMessage, receives framebuffers via postMessage (transferred),
 * paints them to a Canvas.
 *
 * No CORS headers required.  No OPFS, no SharedArrayBuffer.
 *
 * Usage:
 *   import { CircuitPythonHost } from './main-ipc.js';
 *   const host = new CircuitPythonHost(canvas, {
 *       wasmUrl: 'circuitpython-worker.wasm',
 *       workerUrl: 'worker-ipc.js',
 *   });
 *   host.start();
 */

export class CircuitPythonHost {
    constructor(canvas, options = {}) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d');
        this.wasmUrl = options.wasmUrl || 'circuitpython-worker.wasm';
        this.workerUrl = options.workerUrl || 'worker-ipc.js';
        this.worker = null;
        this.width = 0;
        this.height = 0;
        this.frameCount = 0;
        this.imageData = null;

        // Callbacks
        this.onReady = options.onReady || null;
        this.onStdout = options.onStdout || null;
        this.onExit = options.onExit || null;
        this.onError = options.onError || null;
        this.onHwDiff = options.onHwDiff || null;

        // Bind keyboard handler
        this._onKeyDown = this._handleKeyDown.bind(this);
    }

    start() {
        this.worker = new Worker(this.workerUrl);
        this.worker.onmessage = (e) => this._handleMessage(e.data);
        // Resolve wasmUrl to absolute — Worker's fetch() resolves relative
        // to the Worker script's URL, not the page's URL.
        const absoluteWasmUrl = new URL(this.wasmUrl, location.href).href;
        this.worker.postMessage({ type: 'init', wasmUrl: absoluteWasmUrl });

        // Focus canvas and attach keyboard
        this.canvas.tabIndex = 0;
        this.canvas.addEventListener('keydown', this._onKeyDown);
        this.canvas.focus();
    }

    stop() {
        if (this.worker) {
            this.worker.postMessage({ type: 'stop' });
        }
        this.canvas.removeEventListener('keydown', this._onKeyDown);
    }

    sendKey(charCode) {
        if (this.worker) {
            this.worker.postMessage({ type: 'key', char: charCode });
        }
    }

    sendKeys(charCodes) {
        if (this.worker) {
            this.worker.postMessage({ type: 'keys', chars: charCodes });
        }
    }

    _handleMessage(msg) {
        switch (msg.type) {
            case 'ready':
                this.width = msg.width;
                this.height = msg.height;
                this.canvas.width = this.width;
                this.canvas.height = this.height;
                this.imageData = this.ctx.createImageData(this.width, this.height);
                if (this.onReady) this.onReady(this.width, this.height);
                break;

            case 'frame':
                this._paintFrame(msg.fb, msg.width, msg.height);
                break;

            case 'stdout':
                if (this.onStdout) this.onStdout(msg.text, msg.fd);
                break;

            case 'exit':
                if (this.onExit) this.onExit(msg.code);
                break;

            case 'hw_diff':
                if (this.onHwDiff) this.onHwDiff(msg.data, msg.count);
                break;

            case 'error':
                console.error('[CircuitPython]', msg.message);
                if (this.onError) this.onError(msg.message);
                break;
        }
    }

    _paintFrame(fbBuffer, width, height) {
        if (!this.imageData || width !== this.width || height !== this.height) {
            this.width = width;
            this.height = height;
            this.canvas.width = width;
            this.canvas.height = height;
            this.imageData = this.ctx.createImageData(width, height);
        }

        const fb = new Uint8Array(fbBuffer);
        const rgba = this.imageData.data;

        // RGB565 → RGBA8888
        for (let i = 0, j = 0; i < fb.length; i += 2, j += 4) {
            const lo = fb[i], hi = fb[i + 1];
            rgba[j]     = hi & 0xF8;                          // R
            rgba[j + 1] = ((hi & 0x07) << 5) | ((lo & 0xE0) >> 3); // G
            rgba[j + 2] = (lo & 0x1F) << 3;                   // B
            rgba[j + 3] = 255;                                 // A
        }

        this.ctx.putImageData(this.imageData, 0, 0);
        this.frameCount++;
    }

    _handleKeyDown(e) {
        e.preventDefault();
        let bytes = null;

        if (e.key.length === 1) {
            bytes = [e.key.charCodeAt(0)];
        } else {
            switch (e.key) {
                case 'Enter':     bytes = [0x0D]; break;
                case 'Backspace': bytes = [0x08]; break;
                case 'Tab':       bytes = [0x09]; break;
                case 'Escape':    bytes = [0x1B]; break;
                case 'ArrowUp':   bytes = [0x1B, 0x5B, 0x41]; break;
                case 'ArrowDown': bytes = [0x1B, 0x5B, 0x42]; break;
                case 'ArrowRight':bytes = [0x1B, 0x5B, 0x43]; break;
                case 'ArrowLeft': bytes = [0x1B, 0x5B, 0x44]; break;
                case 'Home':      bytes = [0x1B, 0x5B, 0x48]; break;
                case 'End':       bytes = [0x1B, 0x5B, 0x46]; break;
                case 'Delete':    bytes = [0x1B, 0x5B, 0x33, 0x7E]; break;
            }
        }

        // Ctrl+C
        if (e.ctrlKey && e.key === 'c') bytes = [0x03];
        // Ctrl+D
        if (e.ctrlKey && e.key === 'd') bytes = [0x04];

        if (bytes) {
            this.sendKeys(bytes);
        }
    }
}
