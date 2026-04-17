/**
 * hardware.mjs — Hardware module system for CircuitPython WASM.
 *
 * JS hardware modules process /hal/ MEMFS state at the yield boundary
 * (between cp_step frames).  Each module gets preStep/postStep hooks:
 *
 *   preStep  — inject fresh sensor/input data into MEMFS before VM runs
 *   postStep — read output state from MEMFS after VM yields, update UI
 *
 * Modules register for specific /hal/ paths and receive only their data.
 *
 * Usage:
 *   import { GpioModule, NeoPixelModule } from './hardware.mjs';
 *
 *   const board = await CircuitPython.create({ ... });
 *   board.registerHardware(new GpioModule());
 *   board.registerHardware(new NeoPixelModule(ledCanvas));
 *
 * Virtual I2C devices:
 *   const i2c = board.hardware('i2c');
 *   i2c.addDevice(0x44, new SHT40Device());  // register at address 0x44
 *
 * Virtual sensor inputs:
 *   const analog = board.hardware('analog');
 *   analog.setInputValue(board._wasi, 2, 32768);  // potentiometer at 50%
 *
 *   const gpio = board.hardware('gpio');
 *   gpio.setInputValue(board._wasi, 5, true);  // button press on pin 5
 */

// ── GPIO slot layout (8 bytes/pin) ──
// [0] enabled  [1] direction  [2] value  [3] pull
// [4] open_drain  [5] never_reset  [6-7] reserved
const GPIO_SLOT = 8;
const GPIO_MAX_PINS = 32;

// ── NeoPixel region layout ──
// [0] pin  [1] enabled  [2-3] num_bytes(u16LE)  [4+] pixel data
const NEOPIXEL_HEADER = 4;
const NEOPIXEL_MAX_BYTES = 1024;  // 256 pixels × 4 bytes
const NEOPIXEL_REGION = NEOPIXEL_HEADER + NEOPIXEL_MAX_BYTES;

// ── Analog slot layout (4 bytes/pin) ──
// [0] enabled  [1] is_output  [2-3] value(u16LE)
const ANALOG_SLOT = 4;

// ── PWM slot layout (8 bytes/pin) ──
// [0] enabled  [1] variable_freq  [2-3] duty_cycle(u16LE)  [4-7] frequency(u32LE)
const PWM_SLOT = 8;

/**
 * Base class for hardware modules.  Subclasses override preStep/postStep
 * and declare which /hal/ paths they handle.
 */
export class HardwareModule {
    /** @returns {string} Module name (e.g., 'gpio', 'neopixel') */
    get name() { return 'base'; }

    /**
     * @returns {string[]} /hal/ path prefixes this module handles.
     * Used by the router to dispatch onHardwareWrite/onHardwareRead.
     */
    get paths() { return []; }

    /**
     * Called before cp_step — inject fresh data into MEMFS.
     * @param {WasiMemfs} memfs — the WASI filesystem
     * @param {number} nowMs — current time in ms
     */
    preStep(memfs, nowMs) {}

    /**
     * Called after cp_step — read state from MEMFS, update UI.
     * @param {WasiMemfs} memfs — the WASI filesystem
     * @param {number} nowMs — current time in ms
     */
    postStep(memfs, nowMs) {}

    /**
     * Called when Python writes to a /hal/ path handled by this module.
     * @param {string} path — full path (e.g., '/hal/gpio')
     * @param {Uint8Array} data — bytes written
     */
    onWrite(path, data) {}

    /**
     * Called when Python reads a /hal/ path handled by this module.
     * Return fresh data to override what Python sees, or null for no override.
     * @param {string} path — full path
     * @param {number} offset — file read offset
     * @returns {Uint8Array|null}
     */
    onRead(path, offset) { return null; }
}

/**
 * GPIO hardware module — tracks pin state, fires callbacks on changes.
 *
 * Pin state is an array of { enabled, direction, value, pull, openDrain }
 * objects, updated from /hal/gpio after each cp_step.
 */
export class GpioModule extends HardwareModule {
    /**
     * @param {object} [options]
     * @param {function} [options.onChange] — called with (pin, state) on any change
     */
    constructor(options = {}) {
        super();
        this._onChange = options.onChange || null;
        this._pins = new Array(GPIO_MAX_PINS).fill(null);
    }

    get name() { return 'gpio'; }
    get paths() { return ['/hal/gpio']; }

    /** Get current state of a pin. */
    getPin(pin) {
        return this._pins[pin] || null;
    }

    /** Get all pin states (sparse array, null for unused pins). */
    get pins() { return this._pins; }

    /**
     * Set a pin's input value from JS (e.g., virtual button press).
     * Only works for input pins — writes to MEMFS so Python reads it.
     */
    setInputValue(memfs, pin, value) {
        const data = memfs.readFile('/hal/gpio');
        if (!data || pin * GPIO_SLOT + 2 >= data.length) return;
        const state = this._pins[pin];
        if (!state || !state.enabled || state.direction !== 0) return;
        // Write value byte at offset [2] in pin's slot
        const buf = new Uint8Array(data);
        buf[pin * GPIO_SLOT + 2] = value ? 1 : 0;
        memfs.updateHardwareState('/hal/gpio', buf);
    }

    postStep(memfs, nowMs) {
        const data = memfs.readFile('/hal/gpio');
        if (!data) return;

        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * GPIO_SLOT;
            if (off + GPIO_SLOT > data.length) break;

            const enabled = data[off];
            if (!enabled) {
                if (this._pins[pin] !== null) {
                    this._pins[pin] = null;
                    if (this._onChange) this._onChange(pin, null);
                }
                continue;
            }

            const state = {
                enabled: true,
                direction: data[off + 1],  // 0=input, 1=output
                value: data[off + 2],
                pull: data[off + 3],       // 0=none, 1=up, 2=down
                openDrain: data[off + 4],
            };

            const prev = this._pins[pin];
            const changed = !prev ||
                prev.direction !== state.direction ||
                prev.value !== state.value ||
                prev.pull !== state.pull ||
                prev.openDrain !== state.openDrain;

            this._pins[pin] = state;
            if (changed && this._onChange) {
                this._onChange(pin, state);
            }
        }
    }
}

/**
 * NeoPixel hardware module — reads pixel data after each frame.
 *
 * Parses /hal/neopixel regions and provides pixel arrays per pin.
 * Optionally renders to a canvas.
 */
export class NeoPixelModule extends HardwareModule {
    /**
     * @param {object} [options]
     * @param {function} [options.onUpdate] — called with (pin, pixels) on change
     *        pixels is Array of {r, g, b} objects
     */
    constructor(options = {}) {
        super();
        this._onUpdate = options.onUpdate || null;
        this._strips = new Map();  // pin → { numPixels, pixels: [{r,g,b}] }
    }

    get name() { return 'neopixel'; }
    get paths() { return ['/hal/neopixel']; }

    /** Get pixel data for a pin. Returns { numPixels, pixels: [{r,g,b}] } or null. */
    getStrip(pin) {
        return this._strips.get(pin) || null;
    }

    /** Get all strips. */
    get strips() { return this._strips; }

    postStep(memfs, nowMs) {
        const data = memfs.readFile('/hal/neopixel');
        if (!data) return;

        // Scan pin regions
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const base = pin * NEOPIXEL_REGION;
            if (base + NEOPIXEL_HEADER > data.length) break;

            const enabled = data[base + 1];
            if (!enabled) {
                if (this._strips.has(pin)) {
                    this._strips.delete(pin);
                    if (this._onUpdate) this._onUpdate(pin, null);
                }
                continue;
            }

            const numBytes = data[base + 2] | (data[base + 3] << 8);
            if (numBytes === 0) continue;

            // GRB format: 3 bytes per pixel
            const bpp = numBytes % 4 === 0 && numBytes >= 4 ? 4 : 3;
            const numPixels = Math.floor(numBytes / bpp);
            const pixels = [];

            for (let i = 0; i < numPixels; i++) {
                const off = base + NEOPIXEL_HEADER + i * bpp;
                if (off + bpp > data.length) break;
                // NeoPixel wire order: GRB (or GRBW)
                pixels.push({
                    r: data[off + 1],
                    g: data[off],
                    b: data[off + 2],
                });
            }

            this._strips.set(pin, { numPixels, pixels });
            if (this._onUpdate) this._onUpdate(pin, pixels);
        }
    }

    /**
     * Render a strip as a row of colored circles on a 2D canvas.
     * @param {CanvasRenderingContext2D} ctx — canvas context
     * @param {number} pin — which strip to render
     * @param {object} [options]
     * @param {number} [options.x=0] — starting x position
     * @param {number} [options.y=0] — starting y position
     * @param {number} [options.radius=8] — circle radius
     * @param {number} [options.spacing=4] — gap between circles
     * @param {string} [options.layout='row'] — 'row' or 'ring'
     */
    renderStrip(ctx, pin, options = {}) {
        const strip = this._strips.get(pin);
        if (!strip) return;

        const {
            x = 0, y = 0,
            radius = 8, spacing = 4,
            layout = 'row',
        } = options;

        if (layout === 'ring') {
            const n = strip.numPixels;
            const ringRadius = (n * (radius * 2 + spacing)) / (2 * Math.PI);
            const cx = x + ringRadius + radius;
            const cy = y + ringRadius + radius;
            for (let i = 0; i < n; i++) {
                const angle = (i / n) * 2 * Math.PI - Math.PI / 2;
                const px = cx + Math.cos(angle) * ringRadius;
                const py = cy + Math.sin(angle) * ringRadius;
                this._drawPixel(ctx, strip.pixels[i], px, py, radius);
            }
        } else {
            // Row layout
            for (let i = 0; i < strip.numPixels; i++) {
                const px = x + i * (radius * 2 + spacing) + radius;
                const py = y + radius;
                this._drawPixel(ctx, strip.pixels[i], px, py, radius);
            }
        }
    }

    _drawPixel(ctx, pixel, cx, cy, r) {
        if (!pixel) return;
        ctx.beginPath();
        ctx.arc(cx, cy, r, 0, 2 * Math.PI);
        ctx.fillStyle = `rgb(${pixel.r},${pixel.g},${pixel.b})`;
        ctx.fill();
        // Dark outline for visibility on light backgrounds
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 1;
        ctx.stroke();
    }
}

/**
 * Analog hardware module — tracks ADC/DAC state per pin.
 */
export class AnalogModule extends HardwareModule {
    /**
     * @param {object} [options]
     * @param {function} [options.onChange] — called with (pin, { value, isOutput })
     */
    constructor(options = {}) {
        super();
        this._onChange = options.onChange || null;
        this._pins = new Array(GPIO_MAX_PINS).fill(null);
    }

    get name() { return 'analog'; }
    get paths() { return ['/hal/analog']; }

    /** Get analog state for a pin. */
    getPin(pin) { return this._pins[pin] || null; }
    get pins() { return this._pins; }

    /**
     * Set an analog input value from JS (e.g., virtual potentiometer).
     * @param {WasiMemfs} memfs
     * @param {number} pin
     * @param {number} value — 0-65535 (16-bit)
     */
    setInputValue(memfs, pin, value) {
        const data = memfs.readFile('/hal/analog');
        if (!data || pin * ANALOG_SLOT + ANALOG_SLOT > data.length) return;
        const state = this._pins[pin];
        if (!state || !state.enabled || state.isOutput) return;
        const buf = new Uint8Array(data);
        buf[pin * ANALOG_SLOT + 2] = value & 0xff;
        buf[pin * ANALOG_SLOT + 3] = (value >> 8) & 0xff;
        memfs.updateHardwareState('/hal/analog', buf);
    }

    postStep(memfs, nowMs) {
        const data = memfs.readFile('/hal/analog');
        if (!data) return;

        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * ANALOG_SLOT;
            if (off + ANALOG_SLOT > data.length) break;

            const enabled = data[off];
            if (!enabled) {
                if (this._pins[pin] !== null) {
                    this._pins[pin] = null;
                    if (this._onChange) this._onChange(pin, null);
                }
                continue;
            }

            const state = {
                enabled: true,
                isOutput: data[off + 1] !== 0,
                value: data[off + 2] | (data[off + 3] << 8),
            };

            const prev = this._pins[pin];
            const changed = !prev ||
                prev.value !== state.value ||
                prev.isOutput !== state.isOutput;

            this._pins[pin] = state;
            if (changed && this._onChange) {
                this._onChange(pin, state);
            }
        }
    }
}

/**
 * PWM hardware module — tracks PWM state per pin.
 *
 * Provides brightness as a 0.0–1.0 float derived from duty_cycle / 65535.
 * Use brightness for LED dimming visualization.
 */
export class PwmModule extends HardwareModule {
    /**
     * @param {object} [options]
     * @param {function} [options.onChange] — called with (pin, state) on any change
     *        state includes { dutyCycle, frequency, brightness }
     */
    constructor(options = {}) {
        super();
        this._onChange = options.onChange || null;
        this._pins = new Array(GPIO_MAX_PINS).fill(null);
    }

    get name() { return 'pwm'; }
    get paths() { return ['/hal/pwm']; }

    getPin(pin) { return this._pins[pin] || null; }
    get pins() { return this._pins; }

    /**
     * Get brightness (0.0–1.0) for a pin, derived from duty cycle.
     * Returns 0 for disabled/unknown pins.
     */
    getBrightness(pin) {
        const s = this._pins[pin];
        return s ? s.brightness : 0;
    }

    postStep(memfs, nowMs) {
        const data = memfs.readFile('/hal/pwm');
        if (!data) return;

        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);

        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * PWM_SLOT;
            if (off + PWM_SLOT > data.length) break;

            const enabled = data[off];
            if (!enabled) {
                if (this._pins[pin] !== null) {
                    this._pins[pin] = null;
                    if (this._onChange) this._onChange(pin, null);
                }
                continue;
            }

            const dutyCycle = view.getUint16(off + 2, true);
            const state = {
                enabled: true,
                variableFreq: data[off + 1] !== 0,
                dutyCycle,
                frequency: view.getUint32(off + 4, true),
                brightness: dutyCycle / 65535,
            };

            const prev = this._pins[pin];
            const changed = !prev ||
                prev.dutyCycle !== state.dutyCycle ||
                prev.frequency !== state.frequency;

            this._pins[pin] = state;
            if (changed && this._onChange) {
                this._onChange(pin, state);
            }
        }
    }
}

/**
 * I2CDevice — base class for virtual I2C slave devices.
 *
 * Subclass this and override onWrite/onRead to create virtual sensors.
 * Register space is a 256-byte array (matching real I2C register maps).
 *
 * Example: Virtual temperature sensor at address 0x44
 *   class SHT40 extends I2CDevice {
 *       constructor() {
 *           super();
 *           this.registers[0xFD] = 0x47;  // device ID
 *       }
 *       onWrite(register, data) {
 *           if (register === 0xFD) {
 *               // Trigger measurement — fill result registers
 *               const tempRaw = Math.round((25 + Math.random() * 5) * 100);
 *               this.registers[0x00] = (tempRaw >> 8) & 0xFF;
 *               this.registers[0x01] = tempRaw & 0xFF;
 *           }
 *       }
 *   }
 */
export class I2CDevice {
    constructor() {
        /** Register space (256 bytes, initialized to 0xFF like unconnected I2C). */
        this.registers = new Uint8Array(256).fill(0xFF);
    }

    /**
     * Called when Python writes to this device.
     * Override to handle commands (e.g., trigger measurement).
     * @param {number} register — register address (first byte of write)
     * @param {Uint8Array} data — remaining bytes written
     */
    onWrite(register, data) {}

    /**
     * Called when Python reads from this device.
     * Override to provide fresh data (e.g., sensor readings).
     * Default returns register contents at the given offset.
     * @param {number} register — register address being read
     * @param {number} length — number of bytes requested
     * @returns {Uint8Array} — data to return to Python
     */
    onRead(register, length) {
        return this.registers.slice(register, register + length);
    }
}

/**
 * I2C hardware module — virtual I2C bus with device registry.
 *
 * Common-hal I2C writes transactions to /hal/i2c/dev/{addr_dec}.
 * This module intercepts those writes and routes them to registered
 * virtual I2C devices.
 *
 * Write format: file content IS the register space.
 *   - Python writes: [reg_addr, data...] → seeks to reg_addr, writes data
 *   - Python reads: reads from current file position
 *
 * The module keeps device register files in sync with the virtual
 * device register arrays.
 */
export class I2cModule extends HardwareModule {
    constructor() {
        super();
        this._devices = new Map();  // address (number) → I2CDevice
    }

    get name() { return 'i2c'; }
    get paths() { return ['/hal/i2c']; }

    /**
     * Register a virtual I2C device at an address.
     * @param {number} address — 7-bit I2C address (0-127)
     * @param {I2CDevice} device — virtual device instance
     */
    addDevice(address, device) {
        this._devices.set(address, device);
    }

    /**
     * Remove a virtual I2C device.
     * @param {number} address
     */
    removeDevice(address) {
        this._devices.delete(address);
    }

    /** Get a registered device by address. */
    getDevice(address) {
        return this._devices.get(address) || null;
    }

    /** Get all registered devices. */
    get devices() { return this._devices; }

    /**
     * Intercept writes to /hal/i2c/dev/{addr}.
     * Format: first byte is register address, remaining bytes are data.
     */
    onWrite(path, data) {
        const addr = this._parseAddr(path);
        if (addr === null) return;

        const device = this._devices.get(addr);
        if (!device || data.length === 0) return;

        const register = data[0];
        const payload = data.slice(1);

        // Write data into device register space
        for (let i = 0; i < payload.length; i++) {
            if (register + i < 256) {
                device.registers[register + i] = payload[i];
            }
        }

        // Notify device of the write
        device.onWrite(register, payload);
    }

    /**
     * Intercept reads from /hal/i2c/dev/{addr}.
     * Return device register contents so Python sees fresh data.
     */
    onRead(path, offset) {
        const addr = this._parseAddr(path);
        if (addr === null) return null;

        const device = this._devices.get(addr);
        if (!device) return null;

        // Return the full register space — common-hal reads from the
        // file offset that was set by the previous write (register address).
        return device.registers;
    }

    /**
     * Seed device files into MEMFS so probe() finds them.
     * Call this after WASI init but before cp_init.
     * @param {WasiMemfs} memfs
     */
    seedDeviceFiles(memfs) {
        for (const [addr, device] of this._devices) {
            const path = `/hal/i2c/dev/${addr}`;
            memfs.updateHardwareState(path, device.registers);
        }
    }

    preStep(memfs, nowMs) {
        // Sync device register arrays → MEMFS before VM reads them
        for (const [addr, device] of this._devices) {
            const path = `/hal/i2c/dev/${addr}`;
            memfs.updateHardwareState(path, device.registers);
        }
    }

    _parseAddr(path) {
        // /hal/i2c/dev/68 → 68
        const m = path.match(/\/hal\/i2c\/dev\/(\d+)/);
        return m ? parseInt(m[1], 10) : null;
    }
}

/**
 * HardwareRouter — manages registered hardware modules and routes
 * /hal/ callbacks from WasiMemfs to the appropriate module.
 */
export class HardwareRouter {
    constructor() {
        this._modules = [];
        this._pathMap = new Map();  // path prefix → module
    }

    /**
     * Register a hardware module.
     * @param {HardwareModule} mod
     */
    register(mod) {
        this._modules.push(mod);
        for (const path of mod.paths) {
            this._pathMap.set(path, mod);
        }
    }

    /**
     * Get a registered module by name.
     * @param {string} name
     * @returns {HardwareModule|null}
     */
    get(name) {
        return this._modules.find(m => m.name === name) || null;
    }

    /** Run all preStep hooks. */
    preStep(memfs, nowMs) {
        for (const mod of this._modules) {
            mod.preStep(memfs, nowMs);
        }
    }

    /** Run all postStep hooks. */
    postStep(memfs, nowMs) {
        for (const mod of this._modules) {
            mod.postStep(memfs, nowMs);
        }
    }

    /**
     * Route a hardware write to the appropriate module.
     * Called from WasiMemfs onHardwareWrite.
     */
    onWrite(path, data) {
        const mod = this._findModule(path);
        if (mod) mod.onWrite(path, data);
    }

    /**
     * Route a hardware read to the appropriate module.
     * Called from WasiMemfs onHardwareRead.
     */
    onRead(path, offset) {
        const mod = this._findModule(path);
        if (mod) return mod.onRead(path, offset);
        return null;
    }

    _findModule(path) {
        // Check exact match first, then prefix match
        if (this._pathMap.has(path)) return this._pathMap.get(path);
        for (const [prefix, mod] of this._pathMap) {
            if (path.startsWith(prefix)) return mod;
        }
        return null;
    }
}
