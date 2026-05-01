/**
 * circuitpython.mjs — main thread API for the three-piece architecture
 *
 * The VM (circuitpython.wasm) runs in a Web Worker.
 * hardware.wasm runs on the main thread (UI reads it directly).
 * The Worker posts outbound packets (serial, gpio, neopixel) each frame.
 * The main thread applies them to hardware.wasm and renders.
 */

import { Display } from './display.mjs';

const GPIO_SLOT = 12;

export class CircuitPython {
    constructor() {
        this._hw = null;        // hardware.wasm instance (main thread)
        this._worker = null;    // Web Worker running vm.wasm
        this._display = null;
        this._canvas = null;
        this._onStdout = null;
        this._onStderr = null;
        this._onFrame = null;
        this._onReady = null;
        this._raf = null;

        // hardware.wasm addresses
        this._hwGpioAddr = 0;
        this._hwNeopixelAddr = 0;

        // VM display info (received from Worker)
        this._vmDisplay = null;
    }

    static async create(options = {}) {
        const cp = new CircuitPython();
        await cp._init(options);
        return cp;
    }

    async _init(options) {
        // ── 1. Load hardware.wasm (main thread) ──
        const hwBytes = await fetch(options.hardwareUrl || 'hardware/hardware.wasm')
            .then(r => r.arrayBuffer());
        const hwModule = await WebAssembly.compile(hwBytes);
        const hwInstance = await WebAssembly.instantiate(hwModule, {});
        this._hw = hwInstance.exports;
        this._hwGpioAddr = this._hw.hw_gpio_addr();
        this._hwNeopixelAddr = this._hw.hw_neopixel_addr();

        // ── 2. Callbacks + display ──
        this._onStdout = options.onStdout || null;
        this._onStderr = options.onStderr || null;
        this._onFrame = options.onFrame || null;
        this._canvas = options.canvas || null;
        this._ctx2d = this._canvas ? this._canvas.getContext('2d') : null;
        this._imageData = null;

        // ── 3. Start Worker with VM ──
        const workerUrl = options.workerUrl
            || new URL('./cp-worker.js', import.meta.url).href;
        this._worker = new Worker(workerUrl, { type: 'module' });
        this._worker.onmessage = (e) => this._handleWorkerMessage(e.data);
        this._worker.onerror = (e) => console.error('Worker error:', e);

        // Send init message
        const readyPromise = new Promise(resolve => { this._onReady = resolve; });
        this._worker.postMessage({
            type: 'init',
            wasmUrl: options.wasmUrl || 'build-browser/circuitpython.wasm',
            files: options.files || {},
        });

        // Wait for Worker to finish init
        await readyPromise;

        // ── 4. Start render loop ──
        this._loop();
    }

    // ── Public API ──

    execFile(path) {
        this._worker.postMessage({ type: 'exec_file', path });
    }

    startRepl() {
        this._worker.postMessage({ type: 'start_repl' });
    }

    ctrlC() {
        this._worker.postMessage({ type: 'ctrl_c' });
    }

    ctrlD() {
        this._worker.postMessage({ type: 'ctrl_d' });
    }

    pushSerial(byte) {
        this._worker.postMessage({ type: 'serial_push', byte });
    }

    setInput(pin, value) {
        // Write to hardware.wasm (for UI rendering)
        this._hw.hw_gpio_set_value(pin, value ? 1 : 0);
        // Tell Worker to update vm.wasm's port_mem
        this._worker.postMessage({ type: 'gpio_input', pin, value: value ? 1 : 0 });
    }

    writeFile(path, content) {
        const data = typeof content === 'string'
            ? new TextEncoder().encode(content) : new Uint8Array(content);
        this._worker.postMessage({ type: 'write_file', path, data: data.buffer }, [data.buffer]);
    }

    stop() {
        this._worker.postMessage({ type: 'stop' });
    }

    /** Read hardware state for external consumers */
    get hwMemory() { return this._hw.memory; }
    get hwGpioAddr() { return this._hwGpioAddr; }
    get hwNeopixelAddr() { return this._hwNeopixelAddr; }

    // ── Worker message handler ──

    _handleWorkerMessage(msg) {
        switch (msg.type) {
            case 'ready':
                if (this._onReady) this._onReady();
                break;

            case 'frame':
                this._applyPacket(msg.packet);
                break;

            case 'stderr':
                if (this._onStderr) this._onStderr(msg.text);
                break;
        }
    }

    // ── Apply outbound packet from Worker ──

    _applyPacket(packet) {
        // Serial TX → display
        if (packet.serial && packet.serial.length > 0 && this._onStdout) {
            this._onStdout(String.fromCharCode(...packet.serial));
        }

        // GPIO → hardware.wasm
        if (packet.gpio) {
            new Uint8Array(this._hw.memory.buffer).set(packet.gpio, this._hwGpioAddr);
        }

        // NeoPixel → hardware.wasm
        if (packet.neopixel) {
            new Uint8Array(this._hw.memory.buffer).set(packet.neopixel, this._hwNeopixelAddr);
        }

        // Framebuffer → canvas (RGB565 → RGBA)
        if (packet.framebuffer && this._ctx2d) {
            const w = packet.fbWidth;
            const h = packet.fbHeight;
            if (!this._imageData || this._imageData.width !== w) {
                this._imageData = this._ctx2d.createImageData(w, h);
                this._canvas.width = w;
                this._canvas.height = h;
            }
            const fb = new Uint16Array(packet.framebuffer.buffer);
            const rgba = this._imageData.data;
            for (let i = 0; i < fb.length; i++) {
                const px = fb[i];
                const j = i * 4;
                rgba[j]     = ((px >> 11) & 0x1F) << 3;
                rgba[j + 1] = ((px >> 5) & 0x3F) << 2;
                rgba[j + 2] = (px & 0x1F) << 3;
                rgba[j + 3] = 255;
            }
            this._ctx2d.putImageData(this._imageData, 0, 0);
        }
    }

    // ── Render loop (main thread, independent of Worker) ──

    _loop() {
        // Notify frame listeners (GPIO grid, NeoPixel strip, etc.)
        if (this._onFrame) this._onFrame();

        this._raf = requestAnimationFrame(() => this._loop());
    }

    destroy() {
        if (this._raf) cancelAnimationFrame(this._raf);
        this._raf = null;
        if (this._worker) this._worker.terminate();
        this._worker = null;
    }
}
