/**
 * circuitpython.mjs — main thread API for the three-piece architecture
 *
 * Sync bus brokers all data except framebuffer:
 *   - Bus: serial_tx/rx, gpio, neopixel, analog, protocol messages
 *   - Direct: framebuffer (one consumer: canvas, pure pixels)
 *
 * The bus provides dirty tracking, multiple consumers, and attachment
 * points for external hardware (WebSerial, cloud, sensor panels).
 */

import { SyncBus } from '../sync/sync.mjs';

// Region sizes (must match VM's port_memory.h)
const GPIO_SLOTS = 32;
const GPIO_SLOT_SIZE = 12;
const GPIO_SIZE = GPIO_SLOTS * GPIO_SLOT_SIZE;
const NEOPIXEL_REGION_SIZE = 4 + 64 * 4;  // header + 64 pixels × 4 bytes
const ANALOG_SLOTS = 8;
const ANALOG_SLOT_SIZE = 4;
const ANALOG_SIZE = ANALOG_SLOTS * ANALOG_SLOT_SIZE;
const SERIAL_RING_SIZE = 4096;
// Framebuffer is direct path (not on bus) — no size constant needed

export class CircuitPython {
    constructor() {
        this._bus = null;       // sync bus (the broker)
        this._busPort = null;   // our port into the bus
        this._worker = null;    // Web Worker running vm.wasm
        this._onStdout = null;
        this._onStderr = null;
        this._onProtocol = null;
        this._onFrame = null;
        this._onReady = null;
        this._raf = null;
        this._fbWidth = 0;
        this._fbHeight = 0;
        this._framebuffer = null;  // Uint8Array RGB565 (direct path, not on bus)
        this._cursor = null;  // { x, y, sx, sy, tly, htiles, gw, gh, scale }
    }

    static async create(options = {}) {
        const cp = new CircuitPython();
        await cp._init(options);
        return cp;
    }

    async _init(options) {
        // ── 1. Create sync bus (the broker — no hardware.wasm needed) ──
        this._bus = await SyncBus.create({
            wasmUrl: options.syncUrl
                || new URL('../sync/sync.wasm', import.meta.url).href,
            shared: false,  // postMessage transport for now
        });

        // Register all regions
        this._bus.region('serial_tx', { size: SERIAL_RING_SIZE, type: 'ring' });
        this._bus.region('serial_rx', { size: SERIAL_RING_SIZE, type: 'ring' });
        this._bus.region('gpio',      { size: GPIO_SIZE, type: 'slots', slotSize: GPIO_SLOT_SIZE });
        this._bus.region('analog',    { size: ANALOG_SIZE, type: 'slots', slotSize: ANALOG_SLOT_SIZE });
        this._bus.region('neopixel',  { size: NEOPIXEL_REGION_SIZE, type: 'slab' });
        // Framebuffer is NOT on the bus — direct path to canvas only

        this._busPort = this._bus.port();

        // ── 2. Callbacks ──
        this._onStdout = options.onStdout || null;
        this._onStderr = options.onStderr || null;
        this._onProtocol = options.onProtocol || null;
        this._onFrame = options.onFrame || null;

        // ── 3. Start Worker with VM ──
        const workerUrl = options.workerUrl
            || new URL('./cp-worker.js', import.meta.url).href;
        this._worker = new Worker(workerUrl, { type: 'module' });
        this._worker.onmessage = (e) => this._handleWorkerMessage(e.data);
        this._worker.onerror = (e) => console.error('Worker error:', e);

        const readyPromise = new Promise(resolve => { this._onReady = resolve; });
        this._worker.postMessage({
            type: 'init',
            wasmUrl: options.wasmUrl || 'build-browser/circuitpython.wasm',
            files: options.files || {},
        });

        await readyPromise;

        // ── 4. Start render loop ──
        this._loop();
    }

    // ── Public API ──

    get bus() { return this._bus; }
    get port() { return this._busPort; }
    get bus() { return this._bus; }
    get fbWidth() { return this._fbWidth; }
    get fbHeight() { return this._fbHeight; }
    get cursor() { return this._cursor; }

    /** Consume the latest framebuffer (direct path, not on bus).
     *  Returns Uint8Array (RGB565) or null if no new frame. */
    consumeFramebuffer() {
        const fb = this._framebuffer;
        this._framebuffer = null;
        return fb;
    }

    execFile(path) {
        this._worker.postMessage({ type: 'exec_file', path });
    }

    startRepl() {
        this._worker.postMessage({ type: 'start_repl' });
    }

    ctrlC() { this._worker.postMessage({ type: 'ctrl_c' }); }
    ctrlD() { this._worker.postMessage({ type: 'ctrl_d' }); }

    pushSerial(byte) {
        this._worker.postMessage({ type: 'serial_push', byte });
    }

    /** Set a GPIO input value. The iframe owns visual state and
     *  notifies us — we just forward to the bus + VM Worker. */
    setInput(pin, value) {
        const slot = new Uint8Array(GPIO_SLOT_SIZE);
        const existing = this._busPort.readSlot('gpio', pin);
        slot.set(existing);
        slot[2] = value ? 1 : 0;
        this._busPort.writeSlot('gpio', pin, slot);
        this._worker.postMessage({ type: 'gpio_input', pin, value: value ? 1 : 0 });
    }

    writeFile(path, content) {
        const data = typeof content === 'string'
            ? new TextEncoder().encode(content) : new Uint8Array(content);
        this._worker.postMessage(
            { type: 'write_file', path, data: data.buffer },
            [data.buffer]
        );
    }

    stop() {
        this._worker.postMessage({ type: 'stop' });
    }

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

    // ── Apply Worker outbound packet ──
    //
    // All data except framebuffer goes through the sync bus broker.
    // Framebuffer is direct (one consumer: canvas, pure pixels).

    _applyPacket(packet) {
        // ── Bus path: serial, GPIO, NeoPixel, analog, protocol ──

        // Serial TX → bus ring
        if (packet.serial && packet.serial.length > 0) {
            this._busPort.push('serial_tx', new Uint8Array(packet.serial));
        }

        // GPIO → bus slots
        if (packet.gpio) {
            const data = new Uint8Array(packet.gpio);
            for (let i = 0; i < GPIO_SLOTS; i++) {
                this._busPort.writeSlot('gpio', i,
                    data.subarray(i * GPIO_SLOT_SIZE, (i + 1) * GPIO_SLOT_SIZE));
            }
        }

        // NeoPixel → bus slab
        if (packet.neopixel) {
            this._busPort.writeSlab('neopixel', new Uint8Array(packet.neopixel));
        }

        // Protocol messages → bus + callback
        if (packet.protocol && packet.protocol.length > 0) {
            if (this._onProtocol) {
                for (const msg of packet.protocol) {
                    this._onProtocol(msg);
                }
            }
        }

        // ── Direct path: framebuffer + cursor (no bus) ──

        if (packet.framebuffer) {
            this._fbWidth = packet.fbWidth || this._fbWidth;
            this._fbHeight = packet.fbHeight || this._fbHeight;
            this._framebuffer = new Uint8Array(packet.framebuffer);
        }

        if (packet.cursor) {
            const c = new DataView(new Uint8Array(packet.cursor).buffer);
            this._cursor = {
                x: c.getUint16(0, true), y: c.getUint16(2, true),
                sx: c.getUint16(4, true), sy: c.getUint16(6, true),
                tly: c.getUint16(8, true), htiles: c.getUint16(10, true),
                gw: c.getUint16(12, true), gh: c.getUint16(14, true),
                scale: c.getUint16(16, true) || 1,
            };
        }
    }

    // ── Render loop ──

    _loop() {
        // Drain serial from bus → display
        const serialBytes = this._busPort.drain('serial_tx');
        if (serialBytes.length > 0 && this._onStdout) {
            this._onStdout(String.fromCharCode(...serialBytes));
        }

        // Notify frame listeners (they read from bus.port)
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
