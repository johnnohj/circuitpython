/**
 * semihosting.js — JS-side FFI for CircuitPython WASM.
 *
 * "Semihosting" is the conceptual frame: JS is the host, WASM is the
 * target.  This module provides the JS side of the shared-memory
 * communication layer.
 *
 * Two mechanisms, both via WASM linear memory (no WASI fd round-trips):
 *
 *   Event injection (JS → C):
 *     pushEvent() / pushKey() write sh_event_t records into a circular
 *     buffer in WASM linear memory.  The supervisor drains them each
 *     cp_step() via sh_drain_event_ring().
 *
 *   State reading (JS → reads C state):
 *     readState() reads the sh_state_t struct that the supervisor
 *     writes at the end of each cp_step().  No WASM export call
 *     needed — just a DataView over linear memory.
 *
 * Future bidirectional FFI (Python ↔ JS object proxies) will use
 * the jsffi/proxy pattern from the MicroPython webassembly port.
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

// Sizes
const SH_STATE_SIZE  = 24;
const SH_EVENT_SIZE  = 8;

/* ------------------------------------------------------------------ */
/* Semihosting handler                                                 */
/* ------------------------------------------------------------------ */

export class Semihosting {
    constructor() {
        this.instance = null;     // Set via setInstance()
    }

    setInstance(instance) {
        this.instance = instance;
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
}

/* ---- Exports for constants (useful for host code) ---- */

export {
    SH_EVT_KEY_DOWN, SH_EVT_KEY_UP, SH_EVT_TIMER_FIRE,
    SH_EVT_FETCH_DONE, SH_EVT_HW_CHANGE, SH_EVT_PERSIST_DONE,
    SH_EVT_RESIZE,
};
