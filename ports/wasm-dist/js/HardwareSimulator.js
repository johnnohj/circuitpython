/*
 * HardwareSimulator.js — Pure JS hardware state machine for wasm-dist
 *
 * Subscribes to BroadcastChannel('python-dist') and handles hardware events
 * produced by the Python blinka shims via _blinka.send() → bc_out.
 *
 * No DOM dependency — state is stored in plain objects so it can be used in
 * Node.js workers (for testing) or browser contexts alike.  DOM rendering is
 * left to the caller via the 'update' event.
 *
 * Usage:
 *   import { HardwareSimulator } from './HardwareSimulator.js';
 *   const hw = new HardwareSimulator();
 *   hw.on('update', ({ cmd, pin, state }) => renderPin(pin, state));
 *   hw.start();
 *
 * Or subscribe to specific events:
 *   hw.on('gpio_write', ({ pin, value }) => led.classList.toggle('on', value));
 *   hw.on('neo_write',  ({ pin, pixels }) => renderPixels(pixels));
 */

import { CHANNEL } from './BroadcastBus.js';

export class HardwareSimulator {
    constructor() {
        this._pins      = {};   // pin → { direction, value, pull }
        this._neopixels = {};   // pin → { n, order, pixels: [[r,g,b],...] }
        this._displays  = {};   // id  → { width, height, pixels: Uint8Array|null }
        this._pwm       = {};   // pin → { duty_cycle, frequency }
        this._analog     = {};   // pin → { direction, value }
        this._buses      = {};   // id  → { type:'i2c'|'spi', config, devices: Map }
        this._listeners = {};   // event → Set<cb>
        this._bc        = null;
    }

    /**
     * Start listening for hardware events on BroadcastChannel.
     * Safe to call multiple times (idempotent).
     */
    start() {
        if (this._bc) { return; }
        try {
            this._bc = new BroadcastChannel(CHANNEL);
            this._bc.onmessage = (e) => this._onMessage(e.data);
        } catch {
            // BroadcastChannel not available (e.g. some Node environments)
            this._bc = null;
        }
    }

    /** Stop listening and release the BroadcastChannel. */
    stop() {
        if (this._bc) { this._bc.close(); this._bc = null; }
    }

    /**
     * Subscribe to a hardware or simulator event.
     *
     * Hardware events (mirror bc_out cmd field):
     *   'gpio_init'   { pin, direction, pull? }
     *   'gpio_write'  { pin, value }
     *   'gpio_deinit' { pin }
     *   'neo_init'    { pin, n, order }
     *   'neo_write'   { pin, pixels }
     *   'neo_deinit'  { pin }
     *
     * Meta events:
     *   'update'      { cmd, pin, state }  — fires after every hw event
     *
     * @param {string}   event
     * @param {Function} cb
     * @returns {Function} Unsubscribe
     */
    on(event, cb) {
        if (!this._listeners[event]) { this._listeners[event] = new Set(); }
        this._listeners[event].add(cb);
        return () => this._listeners[event].delete(cb);
    }

    /** Current state of a GPIO pin, or undefined if not initialised. */
    getPin(pin) {
        return this._pins[pin];
    }

    /** Current NeoPixel state, or undefined if not initialised. */
    getNeoPixel(pin) {
        return this._neopixels[pin];
    }

    /** All known GPIO pin states as a plain object. */
    get pins() { return { ...this._pins }; }

    /** All known NeoPixel states as a plain object. */
    get neopixels() { return { ...this._neopixels }; }

    /** Current PWM state by pin. */
    getPWM(pin) { return this._pwm[pin]; }
    get pwm() { return { ...this._pwm }; }

    /** Current analog state by pin. */
    getAnalog(pin) { return this._analog[pin]; }
    get analog() { return { ...this._analog }; }

    /** Current bus state by id. */
    getBus(id) { return this._buses[id]; }
    get buses() { return { ...this._buses }; }

    /**
     * Register a virtual I2C device on a bus.
     * The handler is called for each I2C transaction and should return response data.
     * @param {string|null} busId  e.g. 'i2c1', or null for all buses
     * @param {number} address     7-bit I2C address
     * @param {Function} handler   (cmd, ev) => Uint8Array|true|null
     */
    addI2CDevice(busId, address, handler) {
        if (busId) {
            if (!this._buses[busId]) {
                this._buses[busId] = { type: 'i2c', config: {}, devices: new Map() };
            }
            this._buses[busId].devices.set(address, handler);
        }
        // Store globally for auto-registration on future buses
        if (!this._globalI2CDevices) this._globalI2CDevices = new Map();
        this._globalI2CDevices.set(address, handler);
    }

    /**
     * Register a virtual SPI device on a bus.
     * @param {string} busId
     * @param {Function} handler  (cmd, data) => responseBytes or null
     */
    addSPIDevice(busId, handler) {
        if (!this._buses[busId]) {
            this._buses[busId] = { type: 'spi', config: {}, devices: new Map() };
        }
        this._buses[busId].devices.set('default', handler);
    }

    /** Current display state by id, or undefined if not initialised. */
    getDisplay(id) {
        return this._displays[id];
    }

    /** All known display states as a plain object. */
    get displays() { return { ...this._displays }; }

    /**
     * Read a single pixel from a display's framebuffer.
     * @param {string} id    Display id (e.g. 'disp1')
     * @param {number} x
     * @param {number} y
     * @returns {[number,number,number]|null}  [r,g,b] or null
     */
    getDisplayPixel(id, x, y) {
        const d = this._displays[id];
        if (!d || !d.pixels) { return null; }
        if (x < 0 || x >= d.width || y < 0 || y >= d.height) { return null; }
        const off = (y * d.width + x) * 3;
        return [d.pixels[off], d.pixels[off + 1], d.pixels[off + 2]];
    }

    // ── Internal ──────────────────────────────────────────────────────────────

    _onMessage(msg) {
        if (msg.type !== 'hw') { return; }
        this._dispatch(msg);
    }

    _dispatch(ev) {
        let pin   = ev.pin;
        let state = null;

        switch (ev.cmd) {
            case 'gpio_init': {
                this._pins[pin] = {
                    direction: ev.direction ?? 'input',
                    value:     false,
                    pull:      ev.pull ?? null,
                };
                state = this._pins[pin];
                break;
            }
            case 'gpio_write': {
                if (!this._pins[pin]) {
                    this._pins[pin] = { direction: 'output', value: false, pull: null };
                }
                this._pins[pin].value = !!ev.value;
                state = this._pins[pin];
                break;
            }
            case 'gpio_deinit': {
                delete this._pins[pin];
                state = null;
                break;
            }
            case 'neo_init': {
                const n = ev.n ?? 1;
                this._neopixels[pin] = {
                    n,
                    order:  ev.order ?? 'GRB',
                    pixels: Array.from({ length: n }, () => [0, 0, 0]),
                };
                state = this._neopixels[pin];
                break;
            }
            case 'neo_write': {
                if (!this._neopixels[pin]) {
                    const n = (ev.pixels ?? []).length;
                    this._neopixels[pin] = { n, order: 'GRB', pixels: [] };
                }
                this._neopixels[pin].pixels = (ev.pixels ?? []).map(p => [...p]);
                state = this._neopixels[pin];
                break;
            }
            case 'neo_deinit': {
                delete this._neopixels[pin];
                state = null;
                break;
            }
            case 'display_init': {
                const id = ev.id;
                this._displays[id] = {
                    width:  ev.width ?? 128,
                    height: ev.height ?? 64,
                    pixels: null,
                };
                state = this._displays[id];
                // Use id as the "pin" for event emission
                pin = id;
                break;
            }
            case 'display_refresh': {
                const id = ev.id;
                if (!this._displays[id]) {
                    this._displays[id] = {
                        width:  ev.width ?? 128,
                        height: ev.height ?? 64,
                        pixels: null,
                    };
                }
                if (ev.pixels) {
                    this._displays[id].pixels = new Uint8Array(ev.pixels);
                }
                state = this._displays[id];
                pin = id;
                break;
            }
            // ── PWM ──────────────────────────────────────────────────────
            case 'pwm_init':
            case 'pwm_update': {
                this._pwm[pin] = {
                    duty_cycle: ev.duty_cycle ?? 0,
                    frequency:  ev.frequency ?? 500,
                };
                state = this._pwm[pin];
                break;
            }
            case 'pwm_deinit': {
                delete this._pwm[pin];
                state = null;
                break;
            }
            // ── Analog ───────────────────────────────────────────────────
            case 'analog_init': {
                this._analog[pin] = {
                    direction: ev.direction ?? 'input',
                    value: 0,
                };
                state = this._analog[pin];
                break;
            }
            case 'analog_write': {
                if (!this._analog[pin]) {
                    this._analog[pin] = { direction: 'output', value: 0 };
                }
                this._analog[pin].value = ev.value ?? 0;
                state = this._analog[pin];
                break;
            }
            case 'analog_deinit': {
                delete this._analog[pin];
                state = null;
                break;
            }
            // ── I2C ──────────────────────────────────────────────────────
            case 'i2c_init': {
                const id = ev.id;
                if (!this._buses[id]) {
                    this._buses[id] = { type: 'i2c', config: {}, devices: new Map() };
                }
                this._buses[id].config = { scl: ev.scl, sda: ev.sda, frequency: ev.frequency };
                // Auto-register globally-added devices on this new bus
                if (this._globalI2CDevices) {
                    for (const [addr, handler] of this._globalI2CDevices) {
                        this._buses[id].devices.set(addr, handler);
                    }
                }
                state = this._buses[id];
                pin = id;
                break;
            }
            case 'i2c_scan': {
                const id = ev.id;
                const bus = this._buses[id];
                state = bus;
                pin = id;
                if (bus) {
                    ev._scanResult = [...bus.devices.keys()].filter(k => typeof k === 'number');
                }
                break;
            }
            case 'i2c_write':
            case 'i2c_read':
            case 'i2c_write_read': {
                const id = ev.id;
                const bus = this._buses[id];
                pin = id;
                if (bus) {
                    const device = bus.devices.get(ev.addr);
                    if (device) {
                        const response = device(ev.cmd, ev);
                        ev._response = response;
                    }
                }
                state = bus;
                break;
            }
            case 'i2c_deinit': {
                const id = ev.id;
                delete this._buses[id];
                state = null;
                pin = id;
                break;
            }
            // ── SPI ──────────────────────────────────────────────────────
            case 'spi_init': {
                const id = ev.id;
                if (!this._buses[id]) {
                    this._buses[id] = { type: 'spi', config: {}, devices: new Map() };
                }
                this._buses[id].config = { clock: ev.clock, mosi: ev.mosi, miso: ev.miso };
                state = this._buses[id];
                pin = id;
                break;
            }
            case 'spi_configure': {
                const id = ev.id;
                if (this._buses[id]) {
                    Object.assign(this._buses[id].config, {
                        baudrate: ev.baudrate, polarity: ev.polarity,
                        phase: ev.phase, bits: ev.bits,
                    });
                }
                state = this._buses[id];
                pin = id;
                break;
            }
            case 'spi_write':
            case 'spi_read':
            case 'spi_transfer': {
                const id = ev.id;
                const bus = this._buses[id];
                pin = id;
                if (bus) {
                    const device = bus.devices.get('default');
                    if (device) {
                        ev._response = device(ev.cmd, ev);
                    }
                }
                state = bus;
                break;
            }
            case 'spi_deinit': {
                const id = ev.id;
                delete this._buses[id];
                state = null;
                pin = id;
                break;
            }
            default:
                return;   // Unknown command — ignore silently
        }

        // Emit specific-command event, then meta 'update'
        this._emit(ev.cmd, { pin, state, raw: ev });
        this._emit('update', { cmd: ev.cmd, pin, state });
    }

    _emit(event, data) {
        const cbs = this._listeners[event];
        if (!cbs) { return; }
        cbs.forEach(cb => { try { cb(data); } catch {} });
    }
}
