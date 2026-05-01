/**
 * chassis-api.mjs — High-level JS API for the port chassis.
 *
 * Wraps low-level MEMFS access, event ring, and WASM exports into a
 * clean API.  Consumers interact with pins, submit work, and read
 * state without knowing about memory offsets or ring buffer protocols.
 *
 * Usage:
 *   const chassis = await ChassisAPI.create('build/chassis.wasm');
 *   chassis.claimPin(5, 'DIGITAL_IN');
 *   chassis.setPinValue(5, 1);  // JS writes + event
 *   chassis.start();            // rAF loop
 *   chassis.onPinChange = (pin, value) => { ... };
 */

import { WasiChassis } from './wasi-chassis.js';
import * as C from './chassis-constants.mjs';

export class ChassisAPI {
    constructor() {
        this.wasi = null;
        this.instance = null;
        this.exports = null;
        this._running = false;
        this._rafId = null;

        // Callbacks
        this.onPinChange = null;   // (pin, value) — C wrote a pin
        this.onTxData = null;      // (text) — serial TX has data
        this.onWorkDone = null;    // () — work completed
        this.onFrame = null;       // (state) — after each frame
        this.onStdout = null;      // (text) — printf output
    }

    /**
     * Create and initialize a ChassisAPI instance.
     * @param {string|URL} wasmUrl — path to chassis.wasm
     * @returns {Promise<ChassisAPI>}
     */
    static async create(wasmUrl) {
        const api = new ChassisAPI();
        await api._init(wasmUrl);
        return api;
    }

    async _init(wasmUrl) {
        const self = this;

        this.wasi = new WasiChassis({
            onStdout: (text) => {
                if (self.onStdout) self.onStdout(text);
            },
            onStderr: (text) => console.error(text),
        });

        // Fetch and instantiate with FFI imports
        const resp = typeof wasmUrl === 'string' && typeof fetch === 'function'
            ? await fetch(wasmUrl).then(r => r.arrayBuffer())
            : await import('node:fs/promises').then(fs => fs.readFile(new URL(wasmUrl, import.meta.url)));

        const wasiImports = this.wasi.getImports();

        // Add FFI imports (C→JS calls)
        const imports = {
            ...wasiImports,
            ffi: {
                request_frame() {
                    // Schedule a rAF if not already running
                    if (!self._rafId && typeof requestAnimationFrame === 'function') {
                        self._rafId = requestAnimationFrame((now) => self._frame(now));
                    }
                },
                notify(type, pin, arg, data) {
                    self._onNotify(type, pin, arg, data);
                },
            },
        };

        const { instance } = await WebAssembly.instantiate(
            resp instanceof ArrayBuffer ? resp : new Uint8Array(resp),
            imports
        );

        this.wasi.setInstance(instance);
        this.instance = instance;
        this.exports = instance.exports;

        this.exports.chassis_init();
    }

    /* -------------------------------------------------------------- */
    /* Frame loop                                                      */
    /* -------------------------------------------------------------- */

    /** Start the rAF frame loop. */
    start(budgetMs = 13) {
        this._running = true;
        this._budgetMs = budgetMs;
        if (typeof requestAnimationFrame === 'function') {
            this._rafId = requestAnimationFrame((now) => this._frame(now));
        }
    }

    /** Stop the frame loop. */
    stop() {
        this._running = false;
        if (this._rafId) {
            cancelAnimationFrame(this._rafId);
            this._rafId = null;
        }
    }

    /** Run a single frame manually. */
    step(nowMs, budgetMs = 13) {
        return this.exports.chassis_frame(nowMs ?? performance.now(), budgetMs);
    }

    _frame(now) {
        this._rafId = null;
        const rc = this.exports.chassis_frame(now, this._budgetMs);

        if (this.onFrame) {
            this.onFrame(this.readState());
        }

        // If yielded, ffi.request_frame() already scheduled next frame.
        // If running and not yielded, schedule next frame anyway.
        if (this._running && !this._rafId) {
            this._rafId = requestAnimationFrame((n) => this._frame(n));
        }
    }

    /* -------------------------------------------------------------- */
    /* Pin API                                                         */
    /* -------------------------------------------------------------- */

    /**
     * Claim a pin for a role.
     * @param {number} pin
     * @param {string|number} role — 'DIGITAL_IN', 'DIGITAL_OUT', or numeric
     * @returns {boolean} success
     */
    claimPin(pin, role) {
        const roleNum = typeof role === 'string' ? C['ROLE_' + role] : role;
        return this.exports.chassis_claim_pin(pin, roleNum) === 1;
    }

    /** Release a pin. */
    releasePin(pin) {
        this.exports.chassis_release_pin(pin);
    }

    /** Release all pins. */
    releaseAll() {
        this.exports.chassis_release_all();
    }

    /**
     * Set a pin value from JS side.
     * Writes directly to MEMFS and pushes an event.
     */
    setPinValue(pin, value) {
        const gpioView = this.wasi.readFile('/hal/gpio');
        if (!gpioView) return;
        const offset = pin * C.GPIO_SLOT_SIZE;
        gpioView[offset + C.GPIO_VALUE] = value ? 1 : 0;
        gpioView[offset + C.GPIO_FLAGS] |= C.GF_JS_WROTE;
        this._pushEvent(C.EVT_GPIO_CHANGE, pin);
    }

    /**
     * Read a pin value (via export — returns latched for inputs).
     */
    readPin(pin) {
        return this.exports.chassis_read_pin(pin);
    }

    /**
     * Write a pin value from C side (via export).
     */
    writePin(pin, value) {
        this.exports.chassis_write_pin(pin, value);
    }

    /**
     * Get full pin state from MEMFS.
     * @returns {{ enabled, direction, value, pull, role, flags, category, latched }}
     */
    getPinState(pin) {
        const gpioView = this.wasi.readFile('/hal/gpio');
        if (!gpioView) return null;
        const o = pin * C.GPIO_SLOT_SIZE;
        return {
            enabled: gpioView[o + C.GPIO_ENABLED],
            direction: gpioView[o + C.GPIO_DIRECTION],
            value: gpioView[o + C.GPIO_VALUE],
            pull: gpioView[o + C.GPIO_PULL],
            role: gpioView[o + C.GPIO_ROLE],
            roleName: C.ROLE_NAMES[gpioView[o + C.GPIO_ROLE]] || 'UNKNOWN',
            flags: gpioView[o + C.GPIO_FLAGS],
            category: gpioView[o + C.GPIO_CATEGORY],
            latched: gpioView[o + C.GPIO_LATCHED],
        };
    }

    /* -------------------------------------------------------------- */
    /* Work API                                                        */
    /* -------------------------------------------------------------- */

    /** Submit a workload of N items. */
    submitWork(totalItems) {
        this.exports.chassis_submit_work(totalItems);
    }

    /** Check if work is active. */
    workActive() {
        return this.exports.chassis_work_active() === 1;
    }

    /** Get work progress (items completed). */
    workProgress() {
        return this.exports.chassis_work_progress();
    }

    /* -------------------------------------------------------------- */
    /* State reading                                                   */
    /* -------------------------------------------------------------- */

    /** Read port state from MEMFS. */
    readState() {
        const view = this.wasi.readFile('/port/state');
        if (!view) return null;
        const dv = new DataView(view.buffer, view.byteOffset, view.byteLength);
        return {
            phase: dv.getUint32(C.PS_PHASE, true),
            frameCount: dv.getUint32(C.PS_FRAME_COUNT, true),
            nowUs: dv.getUint32(C.PS_NOW_US, true),
            budgetUs: dv.getUint32(C.PS_BUDGET_US, true),
            elapsedUs: dv.getUint32(C.PS_ELAPSED_US, true),
            status: dv.getUint32(C.PS_STATUS, true),
            statusName: C.RC_NAMES[dv.getUint32(C.PS_STATUS, true)] || 'UNKNOWN',
            flags: dv.getUint32(C.PS_FLAGS, true),
        };
    }

    /** Read stack state from MEMFS. */
    readStack() {
        const view = this.wasi.readFile('/port/stack');
        if (!view) return null;
        const dv = new DataView(view.buffer, view.byteOffset, view.byteLength);
        return {
            depth: dv.getUint32(0, true),
            flags: dv.getUint32(4, true),
            active: !!(dv.getUint32(4, true) & C.SF_ACTIVE),
            yielded: !!(dv.getUint32(4, true) & C.SF_YIELDED),
            complete: !!(dv.getUint32(4, true) & C.SF_COMPLETE),
        };
    }

    /** Read raw MEMFS file. */
    readFile(path) {
        return this.wasi.readFile(path);
    }

    /** Get MEMFS alias info (ptr + size). */
    getAliasInfo(path) {
        return this.wasi.getAliasInfo(path);
    }

    /** List all MEMFS files. */
    listFiles() {
        return this.wasi.listFiles();
    }

    /** Get port memory base address and size. */
    memoryLayout() {
        return {
            addr: this.exports.chassis_memory_addr(),
            size: this.exports.chassis_memory_size(),
        };
    }

    /* -------------------------------------------------------------- */
    /* Event ring (JS→C)                                               */
    /* -------------------------------------------------------------- */

    _pushEvent(type, pin, arg = 0, data = 0) {
        const ringInfo = this.wasi.getAliasInfo('/port/event_ring');
        if (!ringInfo) return;
        const mem = this.instance.exports.memory;
        const dv = new DataView(mem.buffer);
        const base = ringInfo.ptr;

        const writeHead = dv.getUint32(base, true);
        const offset = base + C.RING_HEADER_SIZE + writeHead;

        dv.setUint8(offset + 0, type);
        dv.setUint8(offset + 1, pin);
        dv.setUint16(offset + 2, arg, true);
        dv.setUint32(offset + 4, data, true);

        dv.setUint32(base, writeHead + C.EVENT_SIZE, true);
    }

    /* -------------------------------------------------------------- */
    /* C→JS notifications                                              */
    /* -------------------------------------------------------------- */

    _onNotify(type, pin, arg, data) {
        switch (type) {
            case C.NOTIFY_PIN_CHANGED:
                if (this.onPinChange) this.onPinChange(pin, arg);
                break;
            case C.NOTIFY_TX_DATA:
                if (this.onTxData) {
                    const txView = this.wasi.readFile('/hal/serial/tx');
                    if (txView) {
                        const text = new TextDecoder().decode(txView.subarray(8));
                        this.onTxData(text);
                    }
                }
                break;
            case C.NOTIFY_WORK_DONE:
                if (this.onWorkDone) this.onWorkDone();
                break;
            default:
                break;
        }
    }
}
