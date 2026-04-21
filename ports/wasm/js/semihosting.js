/**
 * semihosting.js — JS-side FFI for CircuitPython WASM.
 *
 * "Semihosting" is the conceptual frame: JS is the host, WASM is the
 * target.  This module provides the JS side of the shared-memory
 * communication layer.
 *
 * The event ring is the single bus for all JS → C communication.
 * Context managers submit events; the C supervisor drains and routes them.
 *
 * Two mechanisms, both via WASM linear memory (no WASI fd round-trips):
 *
 *   Event injection (JS → C):
 *     pushEvent() writes sh_event_t records into a circular buffer in
 *     WASM linear memory.  The supervisor drains them each cp_hw_step()
 *     via sh_drain_event_ring() → sh_on_event().
 *
 *   State reading (JS → reads C state):
 *     readState() reads the sh_state_t struct that the supervisor
 *     writes at the end of each cp_hw_step().
 */

/* ------------------------------------------------------------------ */
/* Constants — must match supervisor/semihosting.h                     */
/* ------------------------------------------------------------------ */

// Event types
const SH_EVT_NONE          = 0x00;
const SH_EVT_KEY_DOWN      = 0x01;
const SH_EVT_KEY_UP        = 0x02;
const SH_EVT_TIMER_FIRE    = 0x10;
const SH_EVT_FETCH_DONE    = 0x11;
const SH_EVT_HW_CHANGE     = 0x20;
const SH_EVT_PERSIST_DONE  = 0x30;
const SH_EVT_RESIZE        = 0x40;
const SH_EVT_WAKE          = 0x50;
const SH_EVT_EXEC          = 0x60;
const SH_EVT_CTRL_C        = 0x70;
const SH_EVT_CLEANUP       = 0x80;

// Sizes
const SH_STATE_SIZE  = 44;
const SH_EVENT_SIZE  = 8;
const SH_TRACE_SIZE  = 8;

// Trace event types (C → JS)
const SH_TRACE_LINE       = 0x01;
const SH_TRACE_CALL       = 0x02;
const SH_TRACE_RETURN     = 0x03;
const SH_TRACE_EXCEPTION  = 0x04;

// wasm_frame result codes — packed as (port | sup<<8 | vm<<16)
const WASM_PORT_QUIET      = 0;
const WASM_PORT_EVENTS     = 1;
const WASM_PORT_BG_PENDING = 2;
const WASM_PORT_HW_CHANGED = 3;

const WASM_SUP_IDLE         = 0;
const WASM_SUP_SCHEDULED    = 1;
const WASM_SUP_CTX_DONE     = 2;
const WASM_SUP_ALL_SLEEPING = 3;

const WASM_VM_NOT_RUN    = 0;
const WASM_VM_YIELDED    = 1;
const WASM_VM_SLEEPING   = 2;
const WASM_VM_COMPLETED  = 3;
const WASM_VM_EXCEPTION  = 4;
const WASM_VM_SUSPENDED  = 5;

/* ------------------------------------------------------------------ */
/* Semihosting class                                                   */
/* ------------------------------------------------------------------ */

export class Semihosting {
    constructor() {
        this.instance = null;
    }

    setInstance(instance) {
        this.instance = instance;
    }

    /* ---- Low-level event injection ---- */

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
        entryView.setUint16(2, eventData & 0xFFFF, true);
        entryView.setUint32(4, arg >>> 0, true);

        // Advance write_idx
        headerView.setUint32(0, writeIdx + 1, true);
    }

    /* ---- Typed submit methods (context managers use these) ---- */

    /** Keyboard input — single byte (ASCII or control code). */
    submitKeyDown(keyCode, modifiers = 0) {
        this.pushEvent(SH_EVT_KEY_DOWN, keyCode, modifiers);
    }

    /** Wake a suspended context. ctxId=-1 for broadcast. */
    submitWake(ctxId = 0, reason = 0) {
        this.pushEvent(SH_EVT_WAKE, ctxId & 0xFFFF, reason);
    }

    /** Execute code from the shared input buffer.
     *  kind: 0=string, 1=file path. len: bytes in input buffer. */
    submitExec(kind, len) {
        this.pushEvent(SH_EVT_EXEC, kind, len);
    }

    /** Keyboard interrupt (Ctrl+C). */
    submitCtrlC() {
        this.pushEvent(SH_EVT_CTRL_C, 0, 0);
    }

    /** Layer 3 cleanup — reset pins, buses, display, GC. */
    submitCleanup() {
        this.pushEvent(SH_EVT_CLEANUP, 0, 0);
    }

    /** Timer fired — wake the associated context. */
    submitTimerFire(ctxId = 0) {
        this.pushEvent(SH_EVT_TIMER_FIRE, ctxId & 0xFFFF, 0);
    }

    /** Hardware state changed from JS.
     *  @param {number} pin — pin/channel that changed (matches wake registrations)
     *  @param {number} [halType=0] — hal subsystem (gpio=1, neopixel=2, etc.)
     */
    submitHwChange(pin, halType = 0) {
        this.pushEvent(SH_EVT_HW_CHANGE, pin, halType);
    }

    /** Display resize. */
    submitResize(width, height) {
        this.pushEvent(SH_EVT_RESIZE, width, height);
    }

    /* ---- Wake registrations ---- */

    /** Register a wake condition: when event (type, data) arrives,
     *  wake ctxId. Returns registration id (0-15) or -1 if full.
     *  @param {number} ctxId
     *  @param {number} eventType — SH_EVT_* constant
     *  @param {number} eventData — specific data or 0xFFFF for any
     *  @param {boolean} [oneShot=true] — auto-unregister after first match
     */
    registerWake(ctxId, eventType, eventData = 0xFFFF, oneShot = true) {
        if (!this.instance) return -1;
        return this.instance.exports.cp_register_wake(ctxId, eventType, eventData, oneShot ? 1 : 0);
    }

    /** Unregister a wake condition by id. */
    unregisterWake(regId) {
        if (!this.instance) return;
        this.instance.exports.cp_unregister_wake(regId);
    }

    /** Unregister all wake conditions for a context. */
    unregisterWakeAll(ctxId) {
        if (!this.instance) return;
        this.instance.exports.cp_unregister_wake_all(ctxId);
    }

    /* ---- State reading (JS reads C state) ---- */

    /**
     * Read VM state from WASM linear memory.
     * C fills the struct each cp_hw_step(); JS reads it directly via
     * the exported sh_state_addr() pointer.
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
            bgPending:   view.getUint32(20, true),
            /* Debug/trace fields */
            currentLine: view.getUint32(24, true),
            sourceFile:  view.getUint32(28, true),
            callDepth:   view.getUint32(32, true),
            traceFlags:  view.getUint32(36, true),
            frameResult: view.getUint32(40, true),
        };
    }

    /**
     * Drain trace events from the C → JS trace ring.
     * Returns an array of {type, data, arg} objects.
     * Call periodically (each frame or on demand) when debug is active.
     */
    readTrace() {
        if (!this.instance) return [];
        const exports = this.instance.exports;
        if (!exports.sh_trace_ring_addr) return [];

        const ringAddr = exports.sh_trace_ring_addr();
        const maxEntries = exports.sh_trace_ring_max();
        const mem = exports.memory.buffer;

        const headerView = new DataView(mem, ringAddr, 8);
        const writeIdx = headerView.getUint32(0, true);
        const readIdx  = headerView.getUint32(4, true);

        if (writeIdx === readIdx) return [];  // nothing to read

        const events = [];
        let ri = readIdx;
        while (ri !== writeIdx) {
            const entryOffset = ringAddr + 8 + (ri % maxEntries) * SH_TRACE_SIZE;
            const entryView = new DataView(mem, entryOffset, SH_TRACE_SIZE);
            events.push({
                type: entryView.getUint16(0, true),
                data: entryView.getUint16(2, true),
                arg:  entryView.getUint32(4, true),
            });
            ri++;
        }

        // Advance read index
        headerView.setUint32(4, writeIdx, true);
        return events;
    }
}

/* ---- Exports for constants ---- */

/** Unpack a wasm_frame result into { port, sup, vm }. */
export function unpackFrameResult(r) {
    return { port: r & 0xFF, sup: (r >> 8) & 0xFF, vm: (r >> 16) & 0xFF };
}

export {
    SH_EVT_NONE, SH_EVT_KEY_DOWN, SH_EVT_KEY_UP,
    SH_EVT_TIMER_FIRE, SH_EVT_FETCH_DONE,
    SH_EVT_HW_CHANGE, SH_EVT_PERSIST_DONE,
    SH_EVT_RESIZE, SH_EVT_WAKE,
    SH_EVT_EXEC, SH_EVT_CTRL_C, SH_EVT_CLEANUP,
    SH_TRACE_LINE, SH_TRACE_CALL, SH_TRACE_RETURN, SH_TRACE_EXCEPTION,
    WASM_PORT_QUIET, WASM_PORT_EVENTS, WASM_PORT_BG_PENDING, WASM_PORT_HW_CHANGED,
    WASM_SUP_IDLE, WASM_SUP_SCHEDULED, WASM_SUP_CTX_DONE, WASM_SUP_ALL_SLEEPING,
    WASM_VM_NOT_RUN, WASM_VM_YIELDED, WASM_VM_SLEEPING,
    WASM_VM_COMPLETED, WASM_VM_EXCEPTION, WASM_VM_SUSPENDED,
};
