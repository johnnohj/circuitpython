/**
 * targets.mjs — External hardware targets for CircuitPython WASM.
 *
 * Targets forward /hal/ state changes to real hardware via WebUSB or
 * WebSerial.  They sit alongside the simulated hardware modules — both
 * can run simultaneously (Tee mode).
 *
 * /hal/ fd state is the source of truth.  Targets diff against previous
 * snapshots and translate CHANGES into protocol commands — U2IF reports
 * for WebUSB, raw REPL Python commands for WebSerial.
 *
 * Usage:
 *   import { WebUSBTarget, WebSerialTarget, TeeTarget } from './targets.mjs';
 *
 *   // Connect to a U2IF board via WebUSB (requires user gesture)
 *   const usb = new WebUSBTarget();
 *   await usb.connect();
 *   board.connectTarget(usb);
 *
 *   // Connect to a CircuitPython board via WebSerial
 *   const serial = new WebSerialTarget();
 *   await serial.connect();
 *   board.connectTarget(serial);
 *
 *   // Tee mode: forward to both simulated and real hardware
 *   const tee = new TeeTarget([usb, serial]);
 *   board.connectTarget(tee);
 */

import { GPIO_SLOT, GPIO_MAX_PINS, NEOPIXEL_HEADER, ANALOG_SLOT, PWM_SLOT } from './hardware.mjs';

// ── U2IF opcodes ──
const U2IF = {
    GPIO_INIT:    0x20,
    GPIO_SET:     0x21,
    GPIO_GET:     0x22,
    PWM_INIT:     0x30,
    PWM_DEINIT:   0x31,
    PWM_FREQ:     0x32,
    PWM_DUTY:     0x33,
    ADC_INIT:     0x40,
    ADC_GET:      0x41,
    I2C_INIT:     0x80,
    I2C_DEINIT:   0x81,
    I2C_WRITE:    0x82,
    I2C_READ:     0x83,
    SPI_INIT:     0x60,
    SPI_DEINIT:   0x61,
    SPI_WRITE:    0x62,
    SPI_READ:     0x63,
    NEOPIXEL_INIT:   0xA0,
    NEOPIXEL_DEINIT: 0xA1,
    NEOPIXEL_WRITE:  0xA2,
};

const U2IF_REPORT_SIZE = 64;

// ── Helpers ──

/** Diff two Uint8Arrays by fixed-size slots, return indices of changed slots. */
function diffSlots(prev, curr, slotSize, maxSlots) {
    const changed = [];
    if (!curr) return changed;
    const n = Math.min(maxSlots, Math.floor((curr.length) / slotSize));
    for (let i = 0; i < n; i++) {
        const off = i * slotSize;
        let same = prev && prev.length >= off + slotSize;
        if (same) {
            for (let j = 0; j < slotSize; j++) {
                if (prev[off + j] !== curr[off + j]) { same = false; break; }
            }
        }
        if (!same) changed.push(i);
    }
    return changed;
}

/** Clone a Uint8Array (or return null). */
function snap(data) {
    return data ? new Uint8Array(data) : null;
}

// ═══════════════════════════════════════════════════════════════════
// HardwareTarget — base class
// ═══════════════════════════════════════════════════════════════════

/**
 * Base class for hardware targets.
 *
 * Targets receive /hal/ state snapshots each frame and forward changes
 * to external hardware.  Override the on* methods for your protocol.
 */
export class HardwareTarget {
    constructor() {
        this._connected = false;
        // Previous-frame snapshots for diffing
        this._prevGpio = null;
        this._prevNeopixel = null;
        this._prevAnalog = null;
        this._prevPwm = null;
        // Callbacks for input data from hardware
        this._onInput = null;
    }

    /** @returns {string} Target type name */
    get type() { return 'base'; }

    /** @returns {boolean} Whether the target is connected */
    get connected() { return this._connected; }

    /**
     * Connect to the hardware.  Must be called from a user gesture
     * (click handler) for WebUSB/WebSerial browser security.
     * @returns {Promise<void>}
     */
    async connect() { this._connected = true; }

    /**
     * Disconnect from the hardware.
     * @returns {Promise<void>}
     */
    async disconnect() {
        this._connected = false;
        this._prevGpio = null;
        this._prevNeopixel = null;
        this._prevAnalog = null;
        this._prevPwm = null;
    }

    /**
     * Register a callback for input data from hardware.
     * Called with (type, pin, value) — e.g., ('gpio', 5, 1).
     * @param {function} fn
     */
    onInput(fn) { this._onInput = fn; }

    /**
     * Called each frame with current /hal/ state from MEMFS.
     * Diffs against previous frame, calls on* methods for changes.
     * @param {WasiMemfs} memfs
     * @param {number} nowMs
     */
    applyState(memfs, nowMs) {
        if (!this._connected) return;

        // GPIO
        const gpio = memfs.readFile('/hal/gpio');
        if (gpio) {
            const changed = diffSlots(this._prevGpio, gpio, GPIO_SLOT, GPIO_MAX_PINS);
            for (const pin of changed) {
                const off = pin * GPIO_SLOT;
                const enabled = gpio[off];
                if (enabled) {
                    this.onGpioChange(pin, {
                        direction: gpio[off + 1],
                        value: gpio[off + 2],
                        pull: gpio[off + 3],
                        openDrain: gpio[off + 4],
                    });
                } else {
                    this.onGpioDeinit(pin);
                }
            }
            this._prevGpio = snap(gpio);
        }

        // NeoPixel
        const neo = memfs.readFile('/hal/neopixel');
        if (neo && neo.length >= NEOPIXEL_HEADER) {
            const pin = neo[0];
            const enabled = neo[1];
            const numBytes = neo[2] | (neo[3] << 8);
            if (enabled && numBytes > 0) {
                // Only diff the pixel data region
                const data = neo.subarray(NEOPIXEL_HEADER, NEOPIXEL_HEADER + numBytes);
                let changed = !this._prevNeopixel;
                if (!changed) {
                    const prev = this._prevNeopixel;
                    if (prev.length !== data.length) {
                        changed = true;
                    } else {
                        for (let i = 0; i < data.length; i++) {
                            if (prev[i] !== data[i]) { changed = true; break; }
                        }
                    }
                }
                if (changed) {
                    this.onNeoPixelUpdate(pin, numBytes, data);
                    this._prevNeopixel = snap(data);
                }
            }
        }

        // Analog
        const analog = memfs.readFile('/hal/analog');
        if (analog) {
            const changed = diffSlots(this._prevAnalog, analog, ANALOG_SLOT, GPIO_MAX_PINS);
            for (const pin of changed) {
                const off = pin * ANALOG_SLOT;
                const enabled = analog[off];
                const isOutput = analog[off + 1];
                const value = analog[off + 2] | (analog[off + 3] << 8);
                if (enabled && isOutput) {
                    this.onAnalogOut(pin, value);
                }
            }
            this._prevAnalog = snap(analog);
        }

        // PWM
        const pwm = memfs.readFile('/hal/pwm');
        if (pwm) {
            const changed = diffSlots(this._prevPwm, pwm, PWM_SLOT, GPIO_MAX_PINS);
            for (const pin of changed) {
                const off = pin * PWM_SLOT;
                const enabled = pwm[off];
                if (enabled) {
                    const dutyCycle = pwm[off + 2] | (pwm[off + 3] << 8);
                    const frequency = pwm[off + 4] | (pwm[off + 5] << 8) |
                                     (pwm[off + 6] << 16) | (pwm[off + 7] << 24);
                    this.onPwmChange(pin, dutyCycle, frequency);
                } else {
                    this.onPwmDeinit(pin);
                }
            }
            this._prevPwm = snap(pwm);
        }
    }

    /**
     * Poll the target for input data.  Called periodically from the
     * frame loop.  Override to read from hardware and fire _onInput.
     */
    async pollInputs() {}

    // ── Protocol hooks (override in subclasses) ──

    /** GPIO pin state changed. direction: 0=input, 1=output. */
    onGpioChange(pin, state) {}
    /** GPIO pin deinitialized. */
    onGpioDeinit(pin) {}
    /** NeoPixel data updated.  data is GRB byte array. */
    onNeoPixelUpdate(pin, numBytes, data) {}
    /** Analog output value changed (0–65535). */
    onAnalogOut(pin, value) {}
    /** PWM configuration changed. dutyCycle: 0–65535. */
    onPwmChange(pin, dutyCycle, frequency) {}
    /** PWM pin deinitialized. */
    onPwmDeinit(pin) {}
    /** I2C write request. */
    onI2cWrite(port, addr, data) {}
    /** I2C read request.  Returns Uint8Array or null. */
    onI2cRead(port, addr, length) { return null; }
}

// ═══════════════════════════════════════════════════════════════════
// WebUSBTarget — U2IF protocol over WebUSB
// ═══════════════════════════════════════════════════════════════════

/**
 * Hardware target for U2IF firmware boards via WebUSB.
 *
 * U2IF (USB to Interfaces) turns a Raspberry Pi Pico (or similar) into
 * a USB-to-GPIO/I2C/SPI/NeoPixel bridge.  Commands are 64-byte HID-like
 * reports sent over USB bulk endpoints.
 *
 * Requires browser WebUSB support and a user gesture to connect.
 *
 * @see https://github.com/execuc/u2if
 */
export class WebUSBTarget extends HardwareTarget {
    /**
     * @param {object} [options]
     * @param {number} [options.vendorId=0xCAFE] — USB vendor ID for device filter
     * @param {number} [options.interfaceNum=0] — USB interface number
     * @param {number} [options.endpointOut=1] — OUT endpoint number
     * @param {number} [options.endpointIn=1] — IN endpoint number
     * @param {number} [options.pollIntervalMs=50] — input polling interval
     */
    constructor(options = {}) {
        super();
        this._vendorId = options.vendorId ?? 0xCAFE;
        this._interfaceNum = options.interfaceNum ?? 0;
        this._endpointOut = options.endpointOut ?? 1;
        this._endpointIn = options.endpointIn ?? 1;
        this._pollIntervalMs = options.pollIntervalMs ?? 50;
        this._device = null;
        this._pollTimer = null;
        this._gpioInited = new Set();  // pins already initialized on device
    }

    get type() { return 'webusb'; }

    async connect() {
        if (typeof navigator === 'undefined' || !navigator.usb) {
            throw new Error('WebUSB not available in this environment');
        }

        this._device = await navigator.usb.requestDevice({
            filters: [{ vendorId: this._vendorId }],
        });

        await this._device.open();
        if (this._device.configuration === null) {
            await this._device.selectConfiguration(1);
        }
        await this._device.claimInterface(this._interfaceNum);

        this._connected = true;
        this._startPolling();
    }

    async disconnect() {
        this._stopPolling();
        if (this._device) {
            try {
                await this._device.releaseInterface(this._interfaceNum);
                await this._device.close();
            } catch (e) { /* ignore disconnect errors */ }
            this._device = null;
        }
        this._gpioInited.clear();
        await super.disconnect();
    }

    // ── U2IF report sending ──

    async _sendReport(data) {
        if (!this._device || !this._connected) return;
        // Pad to 64 bytes
        const report = new Uint8Array(U2IF_REPORT_SIZE);
        report.set(data.subarray(0, Math.min(data.length, U2IF_REPORT_SIZE)));
        try {
            await this._device.transferOut(this._endpointOut, report);
        } catch (e) {
            console.error('[WebUSBTarget] send error:', e.message);
        }
    }

    async _recvReport() {
        if (!this._device || !this._connected) return null;
        try {
            const result = await this._device.transferIn(
                this._endpointIn, U2IF_REPORT_SIZE);
            return new Uint8Array(result.data.buffer);
        } catch (e) {
            return null;
        }
    }

    // ── GPIO ──

    onGpioChange(pin, state) {
        // Initialize pin on first use
        if (!this._gpioInited.has(pin)) {
            this._sendReport(new Uint8Array([
                U2IF.GPIO_INIT, pin, state.direction, state.pull, state.value,
            ]));
            this._gpioInited.add(pin);
        } else {
            // Set value
            this._sendReport(new Uint8Array([
                U2IF.GPIO_SET, pin, state.value,
            ]));
        }
    }

    onGpioDeinit(pin) {
        this._gpioInited.delete(pin);
    }

    // ── NeoPixel ──

    onNeoPixelUpdate(pin, numBytes, data) {
        // U2IF NeoPixel write: [opcode, pin, numBytes(u16LE), data...]
        // May need chunking for large strips (>60 bytes payload)
        const chunkSize = U2IF_REPORT_SIZE - 4;  // 60 bytes per report
        let offset = 0;

        while (offset < numBytes) {
            const remaining = numBytes - offset;
            const size = Math.min(remaining, chunkSize);
            const isFirst = offset === 0;

            if (isFirst) {
                // First (or only) chunk: init + write
                const report = new Uint8Array(4 + size);
                report[0] = U2IF.NEOPIXEL_WRITE;
                report[1] = pin;
                report[2] = numBytes & 0xFF;
                report[3] = (numBytes >> 8) & 0xFF;
                report.set(data.subarray(offset, offset + size), 4);
                this._sendReport(report);
            } else {
                // Continuation chunk (raw data, same opcode)
                const report = new Uint8Array(4 + size);
                report[0] = U2IF.NEOPIXEL_WRITE;
                report[1] = pin;
                report[2] = size & 0xFF;
                report[3] = (size >> 8) & 0xFF;
                report.set(data.subarray(offset, offset + size), 4);
                this._sendReport(report);
            }
            offset += size;
        }
    }

    // ── PWM ──

    onPwmChange(pin, dutyCycle, frequency) {
        // Set frequency
        const freqReport = new Uint8Array(6);
        freqReport[0] = U2IF.PWM_FREQ;
        freqReport[1] = pin;
        freqReport[2] = frequency & 0xFF;
        freqReport[3] = (frequency >> 8) & 0xFF;
        freqReport[4] = (frequency >> 16) & 0xFF;
        freqReport[5] = (frequency >> 24) & 0xFF;
        this._sendReport(freqReport);

        // Set duty cycle
        this._sendReport(new Uint8Array([
            U2IF.PWM_DUTY, pin,
            dutyCycle & 0xFF, (dutyCycle >> 8) & 0xFF,
        ]));
    }

    onPwmDeinit(pin) {
        this._sendReport(new Uint8Array([U2IF.PWM_DEINIT, pin]));
    }

    // ── Analog ──

    onAnalogOut(pin, value) {
        // U2IF doesn't have a direct DAC command for most boards;
        // this is a placeholder for boards that support it.
        this._sendReport(new Uint8Array([
            U2IF.ADC_INIT, pin,  // ensure initialized
        ]));
    }

    // ── I2C ──

    onI2cWrite(port, addr, data) {
        // [opcode, port, addr, len_lo, len_hi, data...]
        const report = new Uint8Array(5 + data.length);
        report[0] = U2IF.I2C_WRITE;
        report[1] = port;
        report[2] = addr;
        report[3] = data.length & 0xFF;
        report[4] = (data.length >> 8) & 0xFF;
        report.set(data, 5);
        this._sendReport(report);
    }

    async onI2cRead(port, addr, length) {
        this._sendReport(new Uint8Array([
            U2IF.I2C_READ, port, addr,
            length & 0xFF, (length >> 8) & 0xFF,
        ]));
        const resp = await this._recvReport();
        if (!resp) return null;
        return resp.subarray(1, 1 + length);  // skip status byte
    }

    // ── Input polling ──

    _startPolling() {
        if (this._pollTimer) return;
        this._pollTimer = setInterval(() => this._pollInputs(), this._pollIntervalMs);
    }

    _stopPolling() {
        if (this._pollTimer) {
            clearInterval(this._pollTimer);
            this._pollTimer = null;
        }
    }

    async _pollInputs() {
        if (!this._connected || !this._onInput) return;
        // Poll input pins
        for (const pin of this._gpioInited) {
            this._sendReport(new Uint8Array([U2IF.GPIO_GET, pin]));
            const resp = await this._recvReport();
            if (resp && resp.length >= 2) {
                this._onInput('gpio', resp[0], resp[1]);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// WebSerialTarget — raw REPL protocol over WebSerial
// ═══════════════════════════════════════════════════════════════════

/**
 * Hardware target for CircuitPython boards via WebSerial raw REPL.
 *
 * Translates /hal/ state changes into Python commands executed on a
 * real CircuitPython board.  Slower than U2IF (text protocol over
 * serial) but works with any CircuitPython board — no special firmware.
 *
 * Protocol:
 *   1. Ctrl-A → enter raw REPL
 *   2. Send Python code + Ctrl-D → execute
 *   3. Read "OK" + output + "\x04" + errors + "\x04>"
 *
 * Requires browser WebSerial support and a user gesture to connect.
 */
export class WebSerialTarget extends HardwareTarget {
    /**
     * @param {object} [options]
     * @param {number} [options.baudRate=115200] — serial baud rate
     * @param {number} [options.cmdDelayMs=10] — delay between commands
     */
    constructor(options = {}) {
        super();
        this._baudRate = options.baudRate ?? 115200;
        this._cmdDelayMs = options.cmdDelayMs ?? 10;
        this._port = null;
        this._reader = null;
        this._writer = null;
        this._readBuf = '';
        this._readPromise = null;
        this._initedPins = new Map();  // pin → variable name
        this._initedNeopixel = new Set();
        this._cmdQueue = [];
        this._processing = false;
    }

    get type() { return 'webserial'; }

    async connect() {
        if (typeof navigator === 'undefined' || !navigator.serial) {
            throw new Error('WebSerial not available in this environment');
        }

        this._port = await navigator.serial.requestPort();
        await this._port.open({ baudRate: this._baudRate });

        this._writer = this._port.writable.getWriter();
        this._reader = this._port.readable.getReader();

        // Start background reader
        this._startReader();

        // Enter raw REPL
        await this._enterRawRepl();

        // Import common modules
        await this._exec('import digitalio, analogio, board');

        this._connected = true;
    }

    async disconnect() {
        this._connected = false;
        try {
            // Exit raw REPL (Ctrl-B)
            if (this._writer) {
                await this._write('\x02');
            }
            if (this._reader) {
                await this._reader.cancel();
                this._reader.releaseLock();
                this._reader = null;
            }
            if (this._writer) {
                this._writer.releaseLock();
                this._writer = null;
            }
            if (this._port) {
                await this._port.close();
                this._port = null;
            }
        } catch (e) { /* ignore */ }
        this._initedPins.clear();
        this._initedNeopixel.clear();
        this._cmdQueue = [];
        await super.disconnect();
    }

    // ── Serial I/O ──

    async _write(text) {
        if (!this._writer) return;
        const enc = new TextEncoder();
        await this._writer.write(enc.encode(text));
    }

    _startReader() {
        const dec = new TextDecoder();
        const read = async () => {
            try {
                while (this._reader) {
                    const { value, done } = await this._reader.read();
                    if (done) break;
                    this._readBuf += dec.decode(value);
                    // Resolve any pending read promise
                    if (this._readResolve && this._readBuf.includes('>')) {
                        this._readResolve(this._readBuf);
                        this._readBuf = '';
                        this._readResolve = null;
                    }
                }
            } catch (e) {
                if (this._connected) {
                    console.error('[WebSerialTarget] reader error:', e.message);
                }
            }
        };
        read();
    }

    _waitForPrompt(timeoutMs = 2000) {
        return new Promise((resolve, reject) => {
            // Check if we already have a prompt
            if (this._readBuf.includes('>')) {
                const buf = this._readBuf;
                this._readBuf = '';
                resolve(buf);
                return;
            }
            this._readResolve = resolve;
            setTimeout(() => {
                if (this._readResolve === resolve) {
                    this._readResolve = null;
                    reject(new Error('Timeout waiting for raw REPL prompt'));
                }
            }, timeoutMs);
        });
    }

    async _enterRawRepl() {
        // Send Ctrl-C twice to interrupt, then Ctrl-A for raw REPL
        await this._write('\x03\x03\x01');
        try {
            await this._waitForPrompt(3000);
        } catch (e) {
            console.warn('[WebSerialTarget] raw REPL entry may have failed:', e.message);
        }
    }

    /**
     * Execute Python code on the board via raw REPL.
     * @param {string} code — Python source
     * @returns {Promise<{output: string, error: string}>}
     */
    async _exec(code) {
        // Send code + Ctrl-D to execute
        await this._write(code + '\x04');
        try {
            const response = await this._waitForPrompt();
            // Parse response: OK<output>\x04<error>\x04>
            const okIdx = response.indexOf('OK');
            if (okIdx < 0) return { output: '', error: response };
            const rest = response.substring(okIdx + 2);
            const parts = rest.split('\x04');
            return {
                output: (parts[0] || '').trim(),
                error: (parts[1] || '').trim(),
            };
        } catch (e) {
            return { output: '', error: e.message };
        }
    }

    /**
     * Queue a command for sequential execution.
     * Commands are batched and sent with a small delay between them
     * to avoid overwhelming the serial link.
     */
    _queueCmd(code) {
        this._cmdQueue.push(code);
        if (!this._processing) {
            this._processQueue();
        }
    }

    async _processQueue() {
        this._processing = true;
        while (this._cmdQueue.length > 0 && this._connected) {
            const code = this._cmdQueue.shift();
            await this._exec(code);
            if (this._cmdDelayMs > 0) {
                await new Promise(r => setTimeout(r, this._cmdDelayMs));
            }
        }
        this._processing = false;
    }

    // ── Pin variable management ──

    _pinVar(pin) {
        return `_p${pin}`;
    }

    _ensurePin(pin, direction) {
        const varName = this._pinVar(pin);
        const key = `${pin}:${direction}`;
        if (!this._initedPins.has(pin) || this._initedPins.get(pin) !== key) {
            const dirStr = direction === 1 ? 'OUTPUT' : 'INPUT';
            this._queueCmd(
                `${varName}=digitalio.DigitalInOut(board.GP${pin});` +
                `${varName}.direction=digitalio.Direction.${dirStr}`
            );
            this._initedPins.set(pin, key);
        }
        return varName;
    }

    // ── Protocol hooks ──

    onGpioChange(pin, state) {
        const varName = this._ensurePin(pin, state.direction);
        if (state.direction === 1) {
            // Output: set value
            this._queueCmd(`${varName}.value=${state.value ? 'True' : 'False'}`);
        }
        if (state.pull !== 0) {
            const pullStr = state.pull === 1 ? 'UP' : 'DOWN';
            this._queueCmd(`${varName}.pull=digitalio.Pull.${pullStr}`);
        }
    }

    onGpioDeinit(pin) {
        const varName = this._pinVar(pin);
        if (this._initedPins.has(pin)) {
            this._queueCmd(`${varName}.deinit()`);
            this._initedPins.delete(pin);
        }
    }

    onNeoPixelUpdate(pin, numBytes, data) {
        // Initialize neopixel strip if needed
        if (!this._initedNeopixel.has(pin)) {
            const numPixels = Math.floor(numBytes / 3);
            this._queueCmd(
                `import neopixel;_np${pin}=neopixel.NeoPixel(board.GP${pin},${numPixels})`
            );
            this._initedNeopixel.add(pin);
        }

        // Convert GRB bytes to Python list assignment
        const numPixels = Math.floor(numBytes / 3);
        // For small strips, send individual pixel assignments
        if (numPixels <= 10) {
            for (let i = 0; i < numPixels; i++) {
                const g = data[i * 3];
                const r = data[i * 3 + 1];
                const b = data[i * 3 + 2];
                this._queueCmd(`_np${pin}[${i}]=(${r},${g},${b})`);
            }
            this._queueCmd(`_np${pin}.show()`);
        } else {
            // For large strips, use bytearray bulk write
            const hex = Array.from(data.subarray(0, numBytes))
                .map(b => b.toString(16).padStart(2, '0')).join('');
            this._queueCmd(
                `_np${pin}.buf=bytearray.fromhex('${hex}');_np${pin}.show()`
            );
        }
    }

    onPwmChange(pin, dutyCycle, frequency) {
        this._queueCmd(
            `import pwmio;_pw${pin}=pwmio.PWMOut(board.GP${pin},` +
            `frequency=${frequency},duty_cycle=${dutyCycle})`
        );
    }

    onPwmDeinit(pin) {
        this._queueCmd(`_pw${pin}.deinit()`);
    }

    onAnalogOut(pin, value) {
        this._queueCmd(
            `import analogio;_ao${pin}=analogio.AnalogOut(board.GP${pin});` +
            `_ao${pin}.value=${value}`
        );
    }

    onI2cWrite(port, addr, data) {
        const hex = Array.from(data).map(b => b.toString(16).padStart(2, '0')).join('');
        this._queueCmd(
            `import busio;_i2c=busio.I2C(board.GP1,board.GP0);` +
            `_i2c.writeto(0x${addr.toString(16)},bytearray.fromhex('${hex}'))`
        );
    }

    async onI2cRead(port, addr, length) {
        const result = await this._exec(
            `import busio;_i2c=busio.I2C(board.GP1,board.GP0);` +
            `_b=bytearray(${length});_i2c.readfrom_into(0x${addr.toString(16)},_b);` +
            `print(','.join(str(x) for x in _b))`
        );
        if (!result.output) return null;
        try {
            const bytes = result.output.split(',').map(Number);
            return new Uint8Array(bytes);
        } catch (e) {
            return null;
        }
    }

    // ── Input polling ──

    async pollInputs() {
        if (!this._connected || !this._onInput) return;

        for (const [pin, key] of this._initedPins) {
            if (!key.endsWith(':0')) continue;  // only input pins
            const varName = this._pinVar(pin);
            const result = await this._exec(`print(1 if ${varName}.value else 0)`);
            if (result.output) {
                this._onInput('gpio', pin, parseInt(result.output));
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// TeeTarget — multiplex to multiple targets simultaneously
// ═══════════════════════════════════════════════════════════════════

/**
 * Tee mode: forward state changes to multiple targets simultaneously.
 *
 * All targets receive the same /hal/ state diffs.  Input data from any
 * target is forwarded to the input callback.
 *
 * Usage:
 *   const tee = new TeeTarget([usbTarget, serialTarget]);
 *   board.connectTarget(tee);
 */
export class TeeTarget extends HardwareTarget {
    /**
     * @param {HardwareTarget[]} targets — targets to forward to
     */
    constructor(targets = []) {
        super();
        this._targets = targets;
    }

    get type() { return 'tee'; }

    get targets() { return this._targets; }

    /** Add a target to the tee. */
    addTarget(target) {
        this._targets.push(target);
        if (this._onInput) {
            target.onInput(this._onInput);
        }
    }

    /** Remove a target from the tee. */
    removeTarget(target) {
        const idx = this._targets.indexOf(target);
        if (idx >= 0) this._targets.splice(idx, 1);
    }

    async connect() {
        // Connect all targets that aren't already connected
        for (const t of this._targets) {
            if (!t.connected) await t.connect();
        }
        this._connected = true;
    }

    async disconnect() {
        for (const t of this._targets) {
            if (t.connected) await t.disconnect();
        }
        await super.disconnect();
    }

    onInput(fn) {
        super.onInput(fn);
        // Forward to all targets
        for (const t of this._targets) {
            t.onInput(fn);
        }
    }

    /**
     * Override applyState to forward to all targets.
     * Each target maintains its own diff state.
     */
    applyState(memfs, nowMs) {
        if (!this._connected) return;
        for (const t of this._targets) {
            if (t.connected) t.applyState(memfs, nowMs);
        }
    }

    async pollInputs() {
        for (const t of this._targets) {
            if (t.connected) await t.pollInputs();
        }
    }
}
