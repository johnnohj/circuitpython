/**
 * semihosting.js — JS-side semihosting handler for CircuitPython WASM.
 *
 * Pairs with supervisor/semihosting.c.  When the C side writes to
 * /sys/call, wasi-memfs.js intercepts the fd_write and calls
 * this handler.  The handler either:
 *
 *   - Fulfills synchronously (clock, errno): writes /sys/result
 *     inline before the fd_write returns.
 *
 *   - Starts async work (fetch, timer): marks the request as
 *     acknowledged, does the work, writes /sys/result when done.
 *     The supervisor will see SH_FULFILLED on the next sh_poll().
 *
 * Event injection: call pushEvent() to append to /sys/events.
 * The supervisor drains these during hal_step().
 *
 * State reading: call readState() to parse /sys/state without
 * calling any WASM export.
 *
 * Works in both browser and Node.js environments.
 *
 * Usage with WasiMemfs:
 *
 *   const sh = new Semihosting(wasi);
 *   const wasi = new WasiMemfs({
 *       onSyscall: (callBuf) => sh.handleCall(callBuf),
 *       ...
 *   });
 *   sh.setMemfs(wasi);  // deferred binding
 */

import { env } from './env.js';

/* ------------------------------------------------------------------ */
/* Constants — must match supervisor/semihosting.h                     */
/* ------------------------------------------------------------------ */

// Call status
const SH_IDLE      = 0;
const SH_PENDING   = 1;
const SH_FULFILLED = 2;
const SH_ERROR     = 3;

// Classic semihosting (0x01–0x31)
const SH_SYS_OPEN          = 0x01;
const SH_SYS_CLOSE         = 0x02;
const SH_SYS_WRITEC        = 0x03;
const SH_SYS_WRITE0        = 0x04;
const SH_SYS_WRITE         = 0x05;
const SH_SYS_READ          = 0x06;
const SH_SYS_READC         = 0x07;
const SH_SYS_CLOCK         = 0x10;
const SH_SYS_TIME          = 0x11;
const SH_SYS_ERRNO         = 0x13;
const SH_SYS_EXIT          = 0x18;
const SH_SYS_ELAPSED       = 0x30;
const SH_SYS_TICKFREQ      = 0x31;

// Browser extensions (0x100+)
const SH_SYS_TIMER_SET     = 0x100;
const SH_SYS_TIMER_CANCEL  = 0x101;
const SH_SYS_FETCH         = 0x110;
const SH_SYS_FETCH_STATUS  = 0x111;
const SH_SYS_PERSIST_SYNC  = 0x120;
const SH_SYS_PERSIST_LOAD  = 0x121;
const SH_SYS_DISPLAY_SYNC  = 0x130;
const SH_SYS_HW_IRQ        = 0x140;
const SH_SYS_HW_POLL       = 0x141;
const SH_SYS_VM_STATE      = 0x150;
const SH_SYS_VM_EXCEPTION  = 0x151;
const SH_SYS_DEBUG_LOG     = 0x1F0;

// Event types
const SH_EVT_NONE          = 0x00;
const SH_EVT_KEY_DOWN      = 0x01;
const SH_EVT_KEY_UP        = 0x02;
const SH_EVT_TIMER_FIRE    = 0x10;
const SH_EVT_FETCH_DONE    = 0x11;
const SH_EVT_HW_CHANGE     = 0x20;
const SH_EVT_PERSIST_DONE  = 0x30;
const SH_EVT_RESIZE        = 0x40;

// Sizes
const SH_CALL_SIZE   = 264;
const SH_RESULT_SIZE = 264;
const SH_STATE_SIZE  = 24;
const SH_EVENT_SIZE  = 8;

/* ------------------------------------------------------------------ */
/* Semihosting handler                                                 */
/* ------------------------------------------------------------------ */

export class Semihosting {
    constructor() {
        this.memfs = null;        // Set via setMemfs()
        this.instance = null;     // Set via setInstance()
        this.timers = new Map();  // timer_id → setTimeout handle
        this.fetches = new Map(); // request_id → { status, response }
        this.nextFetchId = 1;
        this.startTime = env.now();

        // User-provided handlers for extensibility
        this.onExit = null;           // (status) => {}
        this.onDebugLog = null;       // (level, message) => {}
        this.onDisplaySync = null;    // (frameCount) => {}
        this.onStateChange = null;    // (state) => {}
    }

    setMemfs(memfs) {
        this.memfs = memfs;
    }

    setInstance(instance) {
        this.instance = instance;
    }

    /* ---- Call dispatch ---- */

    /**
     * Called by wasi-memfs when C writes to /sys/call.
     * @param {Uint8Array} callBuf — the raw 264-byte call record
     */
    handleCall(callBuf) {
        const view = new DataView(callBuf.buffer, callBuf.byteOffset, callBuf.byteLength);
        const call_nr = view.getUint32(0, true);
        const status  = view.getUint32(4, true);

        if (status !== SH_PENDING) return;

        const arg0 = view.getUint32(8,  true);
        const arg1 = view.getUint32(12, true);
        const arg2 = view.getUint32(16, true);
        const arg3 = view.getUint32(20, true);
        const payload = callBuf.slice(24);

        switch (call_nr) {
            // ── Synchronous calls (fulfill inline) ──
            case SH_SYS_CLOCK:
                this._fulfillSync(call_nr,
                    Math.floor((env.now() - this.startTime) / 10), 0);
                break;

            case SH_SYS_TIME:
                this._fulfillSync(call_nr,
                    Math.floor(Date.now() / 1000), 0);
                break;

            case SH_SYS_ELAPSED:
                this._fulfillSync(call_nr,
                    Math.floor(env.now() - this.startTime), 0);
                break;

            case SH_SYS_TICKFREQ:
                this._fulfillSync(call_nr, 1000, 0); // ms precision
                break;

            case SH_SYS_ERRNO:
                this._fulfillSync(call_nr, 0, 0); // no host errno concept
                break;

            case SH_SYS_EXIT:
                if (this.onExit) this.onExit(arg0);
                this._fulfillSync(call_nr, 0, 0);
                break;

            case SH_SYS_DEBUG_LOG:
                this._handleDebugLog(arg0, payload);
                this._fulfillSync(call_nr, 0, 0);
                break;

            case SH_SYS_DISPLAY_SYNC:
                if (this.onDisplaySync) this.onDisplaySync(arg0);
                this._fulfillSync(call_nr, 0, 0);
                break;

            // ── Asynchronous calls (fulfill later) ──
            case SH_SYS_TIMER_SET:
                this._handleTimerSet(call_nr, arg0, arg1);
                break;

            case SH_SYS_TIMER_CANCEL:
                this._handleTimerCancel(call_nr, arg0);
                break;

            case SH_SYS_FETCH:
                this._handleFetch(call_nr, arg0, arg1, arg2, arg3, payload);
                break;

            case SH_SYS_PERSIST_SYNC:
                this._handlePersistSync(call_nr);
                break;

            default:
                console.warn(`[semihosting] unknown call 0x${call_nr.toString(16)}`);
                this._fulfillSync(call_nr, -1, 0);
                break;
        }
    }

    /* ---- Synchronous fulfillment ---- */

    _fulfillSync(call_nr, ret0, ret1, payload = null) {
        const buf = new Uint8Array(SH_RESULT_SIZE);
        const view = new DataView(buf.buffer);
        view.setUint32(0, call_nr, true);
        view.setUint32(4, SH_FULFILLED, true);
        view.setUint32(8, ret0, true);
        view.setUint32(12, ret1, true);

        if (payload) {
            buf.set(payload.slice(0, 248), 16);
        }

        this.memfs.writeFile('/sys/result', buf);
    }

    _fulfillError(call_nr, errno) {
        const buf = new Uint8Array(SH_RESULT_SIZE);
        const view = new DataView(buf.buffer);
        view.setUint32(0, call_nr, true);
        view.setUint32(4, SH_ERROR, true);
        view.setUint32(8, errno, true);

        this.memfs.writeFile('/sys/result', buf);
    }

    /* ---- Timer handling ---- */

    _handleTimerSet(call_nr, timerId, delayMs) {
        // Cancel existing timer with same ID
        if (this.timers.has(timerId)) {
            clearTimeout(this.timers.get(timerId));
        }

        const handle = setTimeout(() => {
            this.timers.delete(timerId);
            // Inject timer event — supervisor picks it up next cp_step()
            this.pushEvent(SH_EVT_TIMER_FIRE, timerId, 0);
        }, delayMs);

        this.timers.set(timerId, handle);
        this._fulfillSync(call_nr, 0, 0);
    }

    _handleTimerCancel(call_nr, timerId) {
        if (this.timers.has(timerId)) {
            clearTimeout(this.timers.get(timerId));
            this.timers.delete(timerId);
        }
        this._fulfillSync(call_nr, 0, 0);
    }

    /* ---- Fetch handling ---- */

    _handleFetch(call_nr, method, urlOffset, urlLen, bodyLen, payload) {
        const methods = ['GET', 'POST', 'PUT', 'DELETE', 'PATCH', 'HEAD'];
        const methodStr = methods[method] || 'GET';

        const decoder = new TextDecoder();
        const url = decoder.decode(payload.slice(0, urlLen));
        const body = bodyLen > 0 ? payload.slice(urlLen, urlLen + bodyLen) : null;

        const requestId = this.nextFetchId++;

        // Fulfill the call immediately with the request ID
        this._fulfillSync(call_nr, requestId, 0);

        // Do the actual fetch asynchronously
        fetch(url, {
            method: methodStr,
            body: body,
        }).then(async (response) => {
            const data = new Uint8Array(await response.arrayBuffer());
            this.fetches.set(requestId, {
                status: response.status,
                data: data,
            });
            this.pushEvent(SH_EVT_FETCH_DONE, requestId, response.status);
        }).catch((err) => {
            this.fetches.set(requestId, { status: 0, data: null });
            this.pushEvent(SH_EVT_FETCH_DONE, requestId, 0);
        });
    }

    /* ---- Persistence ---- */

    _handlePersistSync(call_nr) {
        if (this.memfs && this.memfs.idbBackend) {
            // Flush dirty CIRCUITPY files to IndexedDB
            this.memfs.idbBackend.syncAll(this.memfs).then(() => {
                this.pushEvent(SH_EVT_PERSIST_DONE, 0, 0);
            });
        }
        // Fulfill immediately — the event signals actual completion
        this._fulfillSync(call_nr, 0, 0);
    }

    /* ---- Debug logging ---- */

    _handleDebugLog(level, payload) {
        const decoder = new TextDecoder();
        // Find actual length (stop at first zero or payload end)
        let end = payload.indexOf(0);
        if (end === -1) end = payload.length;
        const message = decoder.decode(payload.slice(0, end));

        if (this.onDebugLog) {
            this.onDebugLog(level, message);
        } else {
            const fn = [console.log, console.warn, console.error][level] || console.log;
            fn(`[cp] ${message}`);
        }
    }

    /* ---- Event injection (JS → Python) ---- */

    /**
     * Write an event directly into the linear-memory event ring.
     * No WASI fd round-trip — safe to call at any time.
     * C drains the ring in hal_step() via sh_drain_event_ring().
     */
    pushEvent(eventType, eventData, arg) {
        if (!this.instance) return;
        const exports = this.instance.exports;
        if (!exports.sh_event_ring_addr) return;

        const ringAddr = exports.sh_event_ring_addr();
        const maxEvents = exports.sh_event_ring_max();
        const mem = exports.memory.buffer;

        // Ring header: [write_idx:u32] [read_idx:u32]
        const headerView = new DataView(mem, ringAddr, 8);
        const writeIdx = headerView.getUint32(0, true);

        // Write event at (writeIdx % max) in the entries array
        const entryOffset = ringAddr + 8 + (writeIdx % maxEvents) * SH_EVENT_SIZE;
        const entryView = new DataView(mem, entryOffset, SH_EVENT_SIZE);
        entryView.setUint16(0, eventType, true);
        entryView.setUint16(2, eventData, true);
        entryView.setUint32(4, arg, true);

        // Advance write_idx
        headerView.setUint32(0, writeIdx + 1, true);
    }

    /**
     * Push a keyboard event.
     */
    pushKey(keyCode, modifiers = 0) {
        this.pushEvent(SH_EVT_KEY_DOWN, keyCode, modifiers);
    }

    /* ---- State reading (JS reads Python state) ---- */

    /**
     * Read VM state from WASM linear memory.
     * C fills the struct each cp_step(); JS reads it directly via
     * the exported sh_state_addr() pointer.  No WASI fd round-trip.
     */
    readState() {
        if (!this.instance) return null;
        const exports = this.instance.exports;
        if (!exports.sh_state_addr) return null;

        const addr = exports.sh_state_addr();
        const view = new DataView(exports.memory.buffer, addr, SH_STATE_SIZE);
        return {
            supState:    view.getUint32(0,  true),
            yieldReason: view.getUint32(4,  true),
            yieldArg:    view.getUint32(8,  true),
            frameCount:  view.getUint32(12, true),
            vmDepth:     view.getUint32(16, true),
            pendingCall: view.getUint32(20, true),
        };
    }

    /**
     * Check if the VM is waiting for a semihosting response.
     */
    hasPendingCall() {
        const state = this.readState();
        return state && state.pendingCall !== 0;
    }
}

/* ---- Exports for constants (useful for host code) ---- */

export {
    SH_IDLE, SH_PENDING, SH_FULFILLED, SH_ERROR,
    SH_EVT_KEY_DOWN, SH_EVT_KEY_UP, SH_EVT_TIMER_FIRE,
    SH_EVT_FETCH_DONE, SH_EVT_HW_CHANGE, SH_EVT_PERSIST_DONE,
    SH_EVT_RESIZE,
    SH_SYS_FETCH, SH_SYS_TIMER_SET,
};
