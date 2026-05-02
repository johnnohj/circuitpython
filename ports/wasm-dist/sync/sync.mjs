/**
 * sync.mjs — frameless memory synchronization service
 *
 * Provides named regions (slab, ring, slots) backed by a tiny WASM binary.
 * Auto-selects transport: SharedArrayBuffer for zero-copy when available,
 * postMessage + Transferable ArrayBuffer when not.
 *
 * Usage:
 *   const bus = await SyncBus.create();
 *   bus.region('serial_tx', { size: 4096, type: 'ring' });
 *   bus.region('gpio', { size: 384, type: 'slots', slotSize: 12 });
 *   const port = bus.port();
 *   port.push('serial_tx', new Uint8Array([72, 105]));
 *   const bytes = port.drain('serial_tx');
 */

const TYPE_SLAB  = 0;
const TYPE_RING  = 1;
const TYPE_SLOTS = 2;

// ── SyncBus ──
// Owns the WASM instance and memory. Creates ports (endpoints).

export class SyncBus {
    constructor() {
        this._instance = null;
        this._memory = null;
        this._exports = null;
        this._regions = new Map();  // name → { index, hash, type }
        this._shared = false;
    }

    /**
     * Create a bus backed by a sync.wasm binary.
     * @param {object} [options]
     * @param {string} [options.wasmUrl] — URL to sync.wasm (default: same dir)
     * @param {boolean} [options.shared] — force SharedArrayBuffer (default: auto-detect)
     */
    static async create(options = {}) {
        const bus = new SyncBus();
        await bus._init(options);
        return bus;
    }

    async _init(options) {
        const canShare = typeof SharedArrayBuffer !== 'undefined'
            && typeof Atomics !== 'undefined';
        this._shared = options.shared ?? canShare;

        const memoryDescriptor = {
            initial: 10,    // 640KB — bus struct + 512KB arena
            maximum: 16,    // grow up to 1MB if needed
        };
        if (this._shared) memoryDescriptor.shared = true;

        this._memory = new WebAssembly.Memory(memoryDescriptor);

        const wasmUrl = options.wasmUrl
            ?? new URL('./sync.wasm', import.meta.url).href;
        const wasmBytes = await fetch(wasmUrl).then(r => r.arrayBuffer());
        const module = await WebAssembly.compile(wasmBytes);
        this._instance = await WebAssembly.instantiate(module, {
            env: { memory: this._memory },
        });
        this._exports = this._instance.exports;
    }

    /** The raw memory buffer. Shared if SAB available. */
    get buffer() { return this._memory.buffer; }

    /** Whether the bus uses SharedArrayBuffer. */
    get isShared() { return this._shared; }

    /**
     * Define a named region.
     * @param {string} name
     * @param {object} opts
     * @param {number} opts.size — data size in bytes
     * @param {'slab'|'ring'|'slots'} [opts.type='slab']
     * @param {number} [opts.slotSize] — required for type='slots'
     * @returns {number} data offset in linear memory
     */
    region(name, opts) {
        const { size, type = 'slab', slotSize = 0 } = opts;
        const typeCode = { slab: TYPE_SLAB, ring: TYPE_RING, slots: TYPE_SLOTS }[type];
        if (typeCode === undefined) throw new Error(`Unknown region type: ${type}`);

        // Write name into WASM memory for hashing
        const nameBytes = new TextEncoder().encode(name);
        const namePtr = this._writeTmp(nameBytes);
        const offset = this._exports.region_create(namePtr, nameBytes.length,
            size, typeCode, slotSize);
        if (offset === 0) throw new Error(`Failed to create region '${name}'`);

        const hash = this._exports.hash_name(namePtr, nameBytes.length);
        const index = this._exports.region_find(hash);
        this._regions.set(name, { index, hash, type: typeCode, offset, size, slotSize });
        return offset;
    }

    /** Create an endpoint port for this bus. */
    port() {
        return new SyncPort(this);
    }

    /**
     * Create a transferable descriptor for sending to a Worker.
     * The Worker calls SyncBus.fromTransfer(desc) to reconstruct.
     */
    transfer() {
        const regions = {};
        for (const [name, info] of this._regions) {
            regions[name] = info;
        }
        return {
            memory: this._memory,
            regions,
            shared: this._shared,
        };
    }

    /**
     * Reconstruct a bus from a transferred descriptor (Worker side).
     * No WASM needed — the Worker reads/writes memory directly.
     */
    static fromTransfer(desc) {
        const bus = new SyncBus();
        bus._memory = desc.memory;
        bus._shared = desc.shared;
        bus._exports = null;  // Worker doesn't need WASM exports for read/write
        for (const [name, info] of Object.entries(desc.regions)) {
            bus._regions.set(name, info);
        }
        return bus;
    }

    // Write bytes to a scratch area for passing strings to WASM.
    // Uses the last 256 bytes of the first memory page.
    _writeTmp(bytes) {
        const tmpOffset = 65536 - 256;
        new Uint8Array(this._memory.buffer).set(bytes, tmpOffset);
        return tmpOffset;
    }

    _regionInfo(name) {
        const info = this._regions.get(name);
        if (!info) throw new Error(`Unknown region: ${name}`);
        return info;
    }
}


// ── SyncPort ──
// An endpoint view over the bus. Reads and writes named regions.
// Tracks local sequence numbers for dirty detection.

export class SyncPort {
    constructor(bus) {
        this._bus = bus;
        this._lastSeq = new Map();  // name → last seen seq
    }

    // ── Dirty detection ──

    /** Has this region changed since we last checked? */
    changed(name) {
        const info = this._bus._regionInfo(name);
        const seq = this._readSeq(info);
        if (!this._lastSeq.has(name)) {
            this._lastSeq.set(name, seq);  // first check: treat as clean
            return false;
        }
        return seq !== this._lastSeq.get(name);
    }

    /** Mark region as "seen" at current sequence. */
    acknowledge(name) {
        const info = this._bus._regionInfo(name);
        this._lastSeq.set(name, this._readSeq(info));
    }

    // ── Slab operations ──

    /** Read entire slab as a typed array view (zero-copy if SAB). */
    read(name) {
        const info = this._bus._regionInfo(name);
        this._lastSeq.set(name, this._readSeq(info));
        return new Uint8Array(this._bus.buffer, info.offset, info.size);
    }

    /** Read slab as Uint16Array (e.g., RGB565 framebuffer). */
    read16(name) {
        const info = this._bus._regionInfo(name);
        this._lastSeq.set(name, this._readSeq(info));
        return new Uint16Array(this._bus.buffer, info.offset, info.size / 2);
    }

    /** Write to slab at byte offset. Marks dirty. */
    write(name, offset, data) {
        const info = this._bus._regionInfo(name);
        new Uint8Array(this._bus.buffer).set(data, info.offset + offset);
        this._markDirty(info);
    }

    /** Write entire slab. Marks dirty. */
    writeSlab(name, data) {
        this.write(name, 0, data);
    }

    // ── Ring operations ──

    /** Push bytes into a ring buffer. Returns bytes written. */
    push(name, bytes) {
        const info = this._bus._regionInfo(name);
        const buf = this._bus.buffer;
        const readHeadPtr = info.offset;      // u32 read_head at start
        const dataBase = info.offset + 4;
        const cap = info.size;
        const view = new DataView(buf);
        const data = new Uint8Array(buf);

        let wh = this._readAux(info);
        const rh = view.getUint32(readHeadPtr, true);
        let written = 0;

        for (let i = 0; i < bytes.length; i++) {
            const next = (wh + 1) % cap;
            if (next === rh) break;  // full
            data[dataBase + wh] = bytes[i];
            wh = next;
            written++;
        }
        if (written > 0) {
            this._writeAux(info, wh);
            this._bumpSeq(info);
        }
        return written;
    }

    /** Drain all available bytes from a ring buffer. */
    drain(name) {
        const info = this._bus._regionInfo(name);
        const buf = this._bus.buffer;
        const readHeadPtr = info.offset;
        const dataBase = info.offset + 4;
        const cap = info.size;
        const view = new DataView(buf);
        const data = new Uint8Array(buf);

        const wh = this._readAux(info);
        let rh = view.getUint32(readHeadPtr, true);

        if (rh === wh) return new Uint8Array(0);

        const result = [];
        while (rh !== wh) {
            result.push(data[dataBase + rh]);
            rh = (rh + 1) % cap;
        }
        view.setUint32(readHeadPtr, rh, true);
        return new Uint8Array(result);
    }

    /** Bytes available to read from ring. */
    available(name) {
        const info = this._bus._regionInfo(name);
        const view = new DataView(this._bus.buffer);
        const rh = view.getUint32(info.offset, true);
        const wh = this._readAux(info);
        return (wh - rh + info.size) % info.size;
    }

    // ── Slot operations ──

    /** Read a single slot as Uint8Array. */
    readSlot(name, slotIndex) {
        const info = this._bus._regionInfo(name);
        const off = info.offset + slotIndex * info.slotSize;
        return new Uint8Array(this._bus.buffer, off, info.slotSize);
    }

    /** Write a slot. Marks slot dirty. */
    writeSlot(name, slotIndex, data) {
        const info = this._bus._regionInfo(name);
        const off = info.offset + slotIndex * info.slotSize;
        new Uint8Array(this._bus.buffer).set(data, off);
        // Set dirty bit
        const view = new DataView(this._bus.buffer);
        const entryBase = 16 + info.index * 32;
        const dirty = view.getUint32(entryBase + 20, true);
        view.setUint32(entryBase + 20, dirty | (1 << slotIndex), true);
        this._bumpSeq(info);
    }

    /** Get dirty slot bitmask and clear it. Returns array of dirty slot indices. */
    dirtySlots(name) {
        const info = this._bus._regionInfo(name);
        const view = new DataView(this._bus.buffer);
        const entryBase = 16 + info.index * 32;
        const mask = view.getUint32(entryBase + 20, true);
        view.setUint32(entryBase + 20, 0, true);
        const slots = [];
        for (let i = 0; i < 32; i++) {
            if (mask & (1 << i)) slots.push(i);
        }
        return slots;
    }

    // ── Global ──

    /** Has anything on the bus changed since last check? */
    get globalChanged() {
        const view = new DataView(this._bus.buffer);
        const seq = view.getUint32(8, true);
        const last = this._lastGlobalSeq ?? -1;
        this._lastGlobalSeq = seq;
        return seq !== last;
    }

    // ── Internal helpers ──

    _readSeq(info) {
        const view = new DataView(this._bus.buffer);
        return view.getUint32(16 + info.index * 32 + 16, true);
    }

    _bumpSeq(info) {
        const view = new DataView(this._bus.buffer);
        const entryBase = 16 + info.index * 32;
        const seq = view.getUint32(entryBase + 16, true) + 1;
        view.setUint32(entryBase + 16, seq, true);
        // Bump global seq
        const gseq = view.getUint32(8, true) + 1;
        view.setUint32(8, gseq, true);
    }

    _markDirty(info) {
        const view = new DataView(this._bus.buffer);
        const entryBase = 16 + info.index * 32;
        view.setUint32(entryBase + 20, 1, true);
        this._bumpSeq(info);
    }

    _readAux(info) {
        const view = new DataView(this._bus.buffer);
        return view.getUint32(16 + info.index * 32 + 28, true);
    }

    _writeAux(info, val) {
        const view = new DataView(this._bus.buffer);
        view.setUint32(16 + info.index * 32 + 28, val, true);
    }
}
