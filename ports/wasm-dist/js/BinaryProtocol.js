/**
 * BinaryProtocol.js — JS-side decode/encode for the binary hardware event protocol.
 *
 * Mirror of binproto.h constants and struct layouts. Used by HardwareSimulator,
 * SensorSimulator, and the OPFS ring buffer reader.
 */

// ---- Type tags ----
export const BP_TYPE_GPIO     = 0x01;
export const BP_TYPE_ANALOG   = 0x02;
export const BP_TYPE_PWM      = 0x03;
export const BP_TYPE_NEOPIXEL = 0x04;
export const BP_TYPE_I2C      = 0x05;
export const BP_TYPE_SPI      = 0x06;
export const BP_TYPE_DISPLAY  = 0x07;
export const BP_TYPE_SLEEP    = 0x08;
export const BP_TYPE_WORKER   = 0x09;
export const BP_TYPE_CUSTOM   = 0xFF;

// ---- Subcommands ----
export const BP_SUB_INIT       = 0x01;
export const BP_SUB_WRITE      = 0x02;
export const BP_SUB_READ       = 0x03;
export const BP_SUB_DEINIT     = 0x04;
export const BP_SUB_SCAN       = 0x02;
export const BP_SUB_WRITE_READ = 0x05;
export const BP_SUB_CONFIGURE  = 0x02;
export const BP_SUB_TRANSFER   = 0x04;
export const BP_SUB_REFRESH    = 0x02;
export const BP_SUB_SPAWN      = 0x01;
export const BP_SUB_REQUEST    = 0x01;

export const BP_HEADER_SIZE = 4;
export const BP_RING_HEADER_SIZE = 16;

// ---- Type tag → name (for debugging) ----
const TYPE_NAMES = {
    [BP_TYPE_GPIO]:     'gpio',
    [BP_TYPE_ANALOG]:   'analog',
    [BP_TYPE_PWM]:      'pwm',
    [BP_TYPE_NEOPIXEL]: 'neopixel',
    [BP_TYPE_I2C]:      'i2c',
    [BP_TYPE_SPI]:      'spi',
    [BP_TYPE_DISPLAY]:  'display',
    [BP_TYPE_SLEEP]:    'time_sleep',
    [BP_TYPE_WORKER]:   'worker_spawn',
    [BP_TYPE_CUSTOM]:   'custom',
};

// Subcommand names are type-dependent since some share numeric values.
// Use a per-type lookup, falling back to generic names.
const GENERIC_SUB_NAMES = {
    0x01: 'init',
    0x02: 'write',
    0x03: 'read',
    0x04: 'deinit',
    0x05: 'write_read',
};
const TYPE_SUB_NAMES = {
    [BP_TYPE_DISPLAY]:  { 0x01: 'init', 0x02: 'refresh', 0x03: 'deinit' },
    [BP_TYPE_SLEEP]:    { 0x01: 'request' },
    [BP_TYPE_WORKER]:   { 0x01: 'spawn' },
    [BP_TYPE_SPI]:      { 0x01: 'init', 0x02: 'configure', 0x03: 'write', 0x04: 'transfer', 0x05: 'deinit' },
    [BP_TYPE_PWM]:      { 0x01: 'init', 0x02: 'update', 0x03: 'deinit' },
};

/**
 * Decode a binary message header from a Uint8Array.
 * @param {Uint8Array} buf
 * @param {number} offset
 * @returns {{ type: number, sub: number, payloadLen: number } | null}
 */
export function decodeHeader(buf, offset = 0) {
    if (buf.length - offset < BP_HEADER_SIZE) return null;
    return {
        type:       buf[offset],
        sub:        buf[offset + 1],
        payloadLen: buf[offset + 2] | (buf[offset + 3] << 8),
    };
}

/**
 * Decode a complete binary message into a JS object matching the current
 * JSON event format (for backward compatibility during migration).
 *
 * @param {Uint8Array} buf — complete message (header + payload)
 * @returns {object} — event object compatible with HardwareSimulator
 */
export function decodeToEvent(buf) {
    const hdr = decodeHeader(buf);
    if (!hdr) return null;

    const dv = new DataView(buf.buffer, buf.byteOffset + BP_HEADER_SIZE, hdr.payloadLen);
    const typeName = TYPE_NAMES[hdr.type] || `unknown_${hdr.type}`;
    const typeSpecific = TYPE_SUB_NAMES[hdr.type];
    const subName = (typeSpecific && typeSpecific[hdr.sub]) ||
                    GENERIC_SUB_NAMES[hdr.sub] ||
                    `sub_${hdr.sub}`;

    const event = { type: 'hw', cmd: `${typeName}_${subName}` };

    switch (hdr.type) {
        case BP_TYPE_GPIO:
            event.pin = dv.getUint8(0);
            event.direction = dv.getUint8(1);
            event.pull = dv.getUint8(2);
            event.value = dv.getUint16(4, true);
            break;

        case BP_TYPE_ANALOG:
            event.pin = dv.getUint8(0);
            event.direction = dv.getUint8(1);
            event.value = dv.getUint16(2, true);
            break;

        case BP_TYPE_PWM:
            event.pin = dv.getUint8(0);
            event.duty_cycle = dv.getUint16(2, true);
            event.frequency = dv.getUint32(4, true);
            break;

        case BP_TYPE_NEOPIXEL:
            event.pin = dv.getUint8(0);
            event.order = dv.getUint8(1);
            event.n = dv.getUint16(2, true);
            if (hdr.payloadLen > 4) {
                event.pixels = new Uint8Array(buf.buffer, buf.byteOffset + BP_HEADER_SIZE + 4, hdr.payloadLen - 4);
            }
            break;

        case BP_TYPE_I2C:
            event.id = dv.getUint8(0);
            event.addr = dv.getUint8(1);
            event.len = dv.getUint16(2, true);
            if (hdr.payloadLen > 4) {
                event.data = new Uint8Array(buf.buffer, buf.byteOffset + BP_HEADER_SIZE + 4, hdr.payloadLen - 4);
            }
            break;

        case BP_TYPE_SPI:
            event.id = dv.getUint8(0);
            event.len = dv.getUint16(2, true);
            if (hdr.payloadLen > 4) {
                event.data = new Uint8Array(buf.buffer, buf.byteOffset + BP_HEADER_SIZE + 4, hdr.payloadLen - 4);
            }
            break;

        case BP_TYPE_DISPLAY:
            event.id = dv.getUint8(0);
            event.width = dv.getUint16(2, true);
            event.height = dv.getUint16(4, true);
            event.fb_offset = dv.getUint32(6, true);
            break;

        case BP_TYPE_SLEEP:
            event.ms = dv.getUint32(0, true);
            break;

        case BP_TYPE_WORKER: {
            event.workerId = dv.getUint16(0, true);
            const funcLen = dv.getUint16(2, true);
            const dec = new TextDecoder();
            event.func = dec.decode(new Uint8Array(buf.buffer, buf.byteOffset + BP_HEADER_SIZE + 4, funcLen));
            break;
        }

        case BP_TYPE_CUSTOM: {
            const dec = new TextDecoder();
            const json = dec.decode(new Uint8Array(buf.buffer, buf.byteOffset + BP_HEADER_SIZE, hdr.payloadLen));
            try {
                return JSON.parse(json);
            } catch {
                event.raw = json;
            }
            break;
        }
    }

    return event;
}

/**
 * Encode a JS object into binary protocol format.
 * @param {number} type — BP_TYPE_* constant
 * @param {number} sub — BP_SUB_* constant
 * @param {Uint8Array|ArrayBuffer} payload — raw payload bytes
 * @returns {Uint8Array} — complete message (header + payload)
 */
export function encode(type, sub, payload) {
    const payloadBytes = payload instanceof Uint8Array ? payload :
                         payload instanceof ArrayBuffer ? new Uint8Array(payload) :
                         new Uint8Array(0);
    const msg = new Uint8Array(BP_HEADER_SIZE + payloadBytes.length);
    msg[0] = type;
    msg[1] = sub;
    msg[2] = payloadBytes.length & 0xFF;
    msg[3] = (payloadBytes.length >> 8) & 0xFF;
    msg.set(payloadBytes, BP_HEADER_SIZE);
    return msg;
}

/**
 * Ring buffer reader for OPFS-backed event streams.
 * Reads from a Uint8Array that starts with bp_ring_header_t.
 */
export class RingReader {
    constructor(buf) {
        this.buf = buf;
        this.dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
    }

    get writeHead() { return this.dv.getUint32(0, true); }
    get readHead()  { return this.dv.getUint32(4, true); }
    set readHead(v) { this.dv.setUint32(4, v, true); }
    get capacity()  { return this.dv.getUint32(8, true); }
    get flags()     { return this.dv.getUint32(12, true); }

    get readable() {
        return (this.writeHead - this.readHead + this.capacity) % this.capacity;
    }

    get pending() {
        return this.readable >= BP_HEADER_SIZE;
    }

    /**
     * Read next message from the ring. Returns Uint8Array or null.
     */
    read() {
        if (this.readable < BP_HEADER_SIZE) return null;

        const data = this.buf;
        const dataStart = BP_RING_HEADER_SIZE;
        const cap = this.capacity;
        let rh = this.readHead;

        // Peek header
        const hdr = new Uint8Array(BP_HEADER_SIZE);
        for (let i = 0; i < BP_HEADER_SIZE; i++) {
            hdr[i] = data[dataStart + rh];
            rh = (rh + 1) % cap;
        }

        const payloadLen = hdr[2] | (hdr[3] << 8);
        const msgSize = BP_HEADER_SIZE + payloadLen;

        if (this.readable < msgSize) return null;

        // Read full message
        rh = this.readHead;
        const msg = new Uint8Array(msgSize);
        for (let i = 0; i < msgSize; i++) {
            msg[i] = data[dataStart + rh];
            rh = (rh + 1) % cap;
        }

        this.readHead = rh;
        return msg;
    }

    /**
     * Drain all pending messages, decode each to an event object.
     * @returns {Array<object>}
     */
    drainToEvents() {
        const events = [];
        let msg;
        while ((msg = this.read()) !== null) {
            const evt = decodeToEvent(msg);
            if (evt) events.push(evt);
        }
        return events;
    }
}
