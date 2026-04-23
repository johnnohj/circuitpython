/**
 * hardware.mjs — Hardware state for CircuitPython WASM.
 *
 * Single HardwareState class reads /hal/ MEMFS state on demand.
 * No shadow arrays — pin_meta in WASM linear memory is the source
 * of truth for role, category, and flags.  MEMFS endpoints hold the
 * raw data (pin values, pixel bytes, register maps).
 *
 * Compatibility: board.hardware('gpio') returns a lightweight view
 * object with the same API as the old per-module classes.
 *
 * Usage:
 *   const gpio = board.hardware('gpio');
 *   gpio.getPin(5);                       // read on demand from MEMFS
 *   gpio.setInputValue(board._wasi, 5, true);
 *
 *   const i2c = board.hardware('i2c');
 *   i2c.addDevice(0x44, new SHT40Device());
 */

// ── Slot layouts (shared with targets.mjs) ──

// GPIO: 8 bytes/pin — [enabled, direction, value, pull, open_drain, never_reset, reserved, reserved]
export const GPIO_SLOT = 8;
export const GPIO_MAX_PINS = 32;

// NeoPixel: header + pixel data per pin region
export const NEOPIXEL_HEADER = 4;
export const NEOPIXEL_MAX_BYTES = 1024;
export const NEOPIXEL_REGION = NEOPIXEL_HEADER + NEOPIXEL_MAX_BYTES;

// Analog: 4 bytes/pin — [enabled, is_output, value_lo, value_hi]
export const ANALOG_SLOT = 4;

// PWM: 8 bytes/pin — [enabled, variable_freq, duty_lo, duty_hi, freq(u32 LE)]
export const PWM_SLOT = 8;

// ── Pin metadata constants (must match supervisor/hal.h) ──

const HAL_FLAG_JS_WROTE = 0x01;
const HAL_FLAG_C_WROTE  = 0x02;
const HAL_FLAG_C_READ   = 0x04;

export const HAL_CAT_NONE     = 0x00;
export const HAL_CAT_DIGITAL  = 0x01;
export const HAL_CAT_ANALOG   = 0x02;
export const HAL_CAT_BUS_UART = 0x03;
export const HAL_CAT_BUS_SPI  = 0x04;
export const HAL_CAT_BUS_I2C  = 0x05;
export const HAL_CAT_NEOPIXEL = 0x06;
export const HAL_CAT_LED      = 0x07;
export const HAL_CAT_BUTTON   = 0x08;

export const HAL_ROLE_UNCLAIMED   = 0x00;
export const HAL_ROLE_DIGITAL_IN  = 0x01;
export const HAL_ROLE_DIGITAL_OUT = 0x02;
export const HAL_ROLE_ADC         = 0x03;
export const HAL_ROLE_DAC         = 0x04;
export const HAL_ROLE_PWM         = 0x05;
export const HAL_ROLE_NEOPIXEL    = 0x06;

// ── I2CDevice base class (user-facing API) ──

/**
 * Base class for virtual I2C slave devices.
 * Subclass and override onWrite/onRead to create virtual sensors.
 */
export class I2CDevice {
    constructor() {
        this.registers = new Uint8Array(256).fill(0xFF);
    }
    onWrite(register, data) {}
    onRead(register, length) {
        return this.registers.slice(register, register + length);
    }
}

// ── HardwareState ──

/**
 * Unified hardware state — reads MEMFS on demand, no shadow arrays.
 * Replaces GpioModule, AnalogModule, PwmModule, NeoPixelModule,
 * I2cModule, and HardwareRouter.
 */
export class HardwareState {
    constructor() {
        this._exports = null;
        this._memfs = null;
        this._pinMetaAddr = 0;
        this._pinMetaStride = 4;
        this._onChange = null;
        this._i2cDevices = new Map();
        this._views = null;
    }

    // ── Lifecycle ──

    setExports(exports) {
        this._exports = exports;
        if (exports.hal_pin_meta_addr) {
            this._pinMetaAddr = exports.hal_pin_meta_addr();
            this._pinMetaStride = exports.hal_pin_meta_stride?.() || 4;
        }
    }

    setMemfs(memfs) { this._memfs = memfs; }

    reset(memfs) {
        const m = memfs || this._memfs;
        if (!m) return;
        for (const path of ['/hal/gpio', '/hal/analog', '/hal/pwm', '/hal/neopixel']) {
            const data = m.readFile(path);
            if (data) data.fill(0);
        }
        for (const [addr, dev] of this._i2cDevices) {
            const data = m.readFile(`/hal/i2c/dev/${addr}`);
            if (data) data.fill(0);
            if (dev.reset) dev.reset();
        }
    }

    // ── Pin meta readers (from WASM linear memory) ──

    getPinMeta(pin) {
        if (!this._exports || pin < 0 || pin >= 64) return null;
        const mem = new Uint8Array(this._exports.memory.buffer);
        const base = this._pinMetaAddr + pin * this._pinMetaStride;
        return {
            role: mem[base],
            flags: mem[base + 1],
            category: mem[base + 2],
            latched: mem[base + 3],
        };
    }

    // ── GPIO (on-demand from MEMFS) ──

    getGpioPin(pin) {
        const data = this._memfs?.readFile('/hal/gpio');
        if (!data || pin * GPIO_SLOT + GPIO_SLOT > data.length) return null;
        const off = pin * GPIO_SLOT;
        if (!data[off]) return null;  // not enabled
        return {
            enabled: true,
            direction: data[off + 1],
            value: data[off + 2],
            pull: data[off + 3],
            openDrain: data[off + 4],
        };
    }

    _readAllGpioPins() {
        const data = this._memfs?.readFile('/hal/gpio');
        const pins = new Array(GPIO_MAX_PINS).fill(null);
        if (!data) return pins;
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * GPIO_SLOT;
            if (off + GPIO_SLOT > data.length) break;
            if (!data[off]) continue;
            pins[pin] = {
                enabled: true,
                direction: data[off + 1],
                value: data[off + 2],
                pull: data[off + 3],
                openDrain: data[off + 4],
            };
        }
        return pins;
    }

    setGpioInput(pin, value) {
        const data = this._memfs?.readFile('/hal/gpio');
        if (!data || pin * GPIO_SLOT + 2 >= data.length) return;
        const off = pin * GPIO_SLOT;
        if (!data[off] || data[off + 1] !== 0) return;  // not enabled or not input

        const buf = new Uint8Array(data);
        buf[off + 2] = value ? 1 : 0;
        this._memfs.updateHardwareState('/hal/gpio', buf, pin);
        if (this._exports?.hal_set_pin_flag) {
            this._exports.hal_set_pin_flag(pin, HAL_FLAG_JS_WROTE);
        }
    }

    // ── Analog (on-demand from MEMFS) ──

    getAnalogPin(pin) {
        const data = this._memfs?.readFile('/hal/analog');
        if (!data || pin * ANALOG_SLOT + ANALOG_SLOT > data.length) return null;
        const off = pin * ANALOG_SLOT;
        if (!data[off]) return null;
        return {
            enabled: true,
            isOutput: data[off + 1] !== 0,
            value: data[off + 2] | (data[off + 3] << 8),
        };
    }

    _readAllAnalogPins() {
        const data = this._memfs?.readFile('/hal/analog');
        const pins = new Array(GPIO_MAX_PINS).fill(null);
        if (!data) return pins;
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * ANALOG_SLOT;
            if (off + ANALOG_SLOT > data.length) break;
            if (!data[off]) continue;
            pins[pin] = {
                enabled: true,
                isOutput: data[off + 1] !== 0,
                value: data[off + 2] | (data[off + 3] << 8),
            };
        }
        return pins;
    }

    setAnalogInput(pin, value) {
        const data = this._memfs?.readFile('/hal/analog');
        if (!data || pin * ANALOG_SLOT + ANALOG_SLOT > data.length) return;
        const off = pin * ANALOG_SLOT;
        if (!data[off] || data[off + 1] !== 0) return;  // not enabled or is output

        const buf = new Uint8Array(data);
        buf[off + 2] = value & 0xff;
        buf[off + 3] = (value >> 8) & 0xff;
        this._memfs.updateHardwareState('/hal/analog', buf, pin);
        if (this._exports?.hal_set_pin_flag) {
            this._exports.hal_set_pin_flag(pin, HAL_FLAG_JS_WROTE);
        }
    }

    // ── PWM (on-demand from MEMFS) ──

    getPwmPin(pin) {
        const data = this._memfs?.readFile('/hal/pwm');
        if (!data || pin * PWM_SLOT + PWM_SLOT > data.length) return null;
        const off = pin * PWM_SLOT;
        if (!data[off]) return null;
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        const dutyCycle = view.getUint16(off + 2, true);
        return {
            enabled: true,
            variableFreq: data[off + 1] !== 0,
            dutyCycle,
            frequency: view.getUint32(off + 4, true),
            brightness: dutyCycle / 65535,
        };
    }

    _readAllPwmPins() {
        const data = this._memfs?.readFile('/hal/pwm');
        const pins = new Array(GPIO_MAX_PINS).fill(null);
        if (!data) return pins;
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * PWM_SLOT;
            if (off + PWM_SLOT > data.length) break;
            if (!data[off]) continue;
            const dutyCycle = view.getUint16(off + 2, true);
            pins[pin] = {
                enabled: true,
                variableFreq: data[off + 1] !== 0,
                dutyCycle,
                frequency: view.getUint32(off + 4, true),
                brightness: dutyCycle / 65535,
            };
        }
        return pins;
    }

    // ── NeoPixel (on-demand from MEMFS) ──

    getNeopixelStrip(pin) {
        const data = this._memfs?.readFile('/hal/neopixel');
        if (!data) return null;
        const base = pin * NEOPIXEL_REGION;
        if (base + NEOPIXEL_HEADER > data.length) return null;
        if (!data[base + 1]) return null;  // not enabled

        const numBytes = data[base + 2] | (data[base + 3] << 8);
        if (numBytes === 0) return null;

        const bpp = numBytes % 4 === 0 && numBytes >= 4 ? 4 : 3;
        const numPixels = Math.floor(numBytes / bpp);
        const pixels = [];
        for (let i = 0; i < numPixels; i++) {
            const off = base + NEOPIXEL_HEADER + i * bpp;
            if (off + bpp > data.length) break;
            pixels.push({ r: data[off + 1], g: data[off], b: data[off + 2] });
        }
        return { numPixels, pixels };
    }

    _readAllNeopixelStrips() {
        const data = this._memfs?.readFile('/hal/neopixel');
        const strips = new Map();
        if (!data) return strips;
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const base = pin * NEOPIXEL_REGION;
            if (base + NEOPIXEL_HEADER > data.length) break;
            if (!data[base + 1]) continue;
            const numBytes = data[base + 2] | (data[base + 3] << 8);
            if (numBytes === 0) continue;
            const bpp = numBytes % 4 === 0 && numBytes >= 4 ? 4 : 3;
            const numPixels = Math.floor(numBytes / bpp);
            const pixels = [];
            for (let i = 0; i < numPixels; i++) {
                const off = base + NEOPIXEL_HEADER + i * bpp;
                if (off + bpp > data.length) break;
                pixels.push({ r: data[off + 1], g: data[off], b: data[off + 2] });
            }
            strips.set(pin, { numPixels, pixels });
        }
        return strips;
    }

    // ── I2C device registry ──

    addDevice(address, device) { this._i2cDevices.set(address, device); }
    removeDevice(address) { this._i2cDevices.delete(address); }
    getDevice(address) { return this._i2cDevices.get(address) || null; }
    get devices() { return this._i2cDevices; }

    seedDeviceFiles(memfs) {
        const m = memfs || this._memfs;
        if (!m) return;
        for (const [addr, device] of this._i2cDevices) {
            m.updateHardwareState(`/hal/i2c/dev/${addr}`, device.registers);
        }
    }

    // ── Routing (replaces HardwareRouter) ──

    onWrite(path, data) {
        if (path.startsWith('/hal/i2c')) {
            this._i2cOnWrite(path, data);
        }
        // onChange callback for UI reactivity (GPIO, analog, PWM, neopixel)
        if (this._onChange) {
            if (path === '/hal/gpio') this._fireGpioChanges(data);
            else if (path === '/hal/analog') this._fireAnalogChanges(data);
            else if (path === '/hal/pwm') this._firePwmChanges(data);
            else if (path === '/hal/neopixel') this._onChange('neopixel', -1, data);
        }
    }

    onRead(path, offset) {
        if (!path.startsWith('/hal/i2c/dev/')) return null;
        const addr = this._parseI2cAddr(path);
        if (addr === null) return null;
        const device = this._i2cDevices.get(addr);
        return device ? device.registers : null;
    }

    afterFrame(memfs) {}  // C latch handles persistence

    // ── onChange helpers — detect C_WROTE flags ──

    _fireGpioChanges(data) {
        if (!data) return;
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * GPIO_SLOT;
            if (off + GPIO_SLOT > data.length) break;
            if (!data[off]) continue;
            this._onChange('gpio', pin, {
                enabled: true,
                direction: data[off + 1],
                value: data[off + 2],
                pull: data[off + 3],
                openDrain: data[off + 4],
            });
        }
    }

    _fireAnalogChanges(data) {
        if (!data) return;
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * ANALOG_SLOT;
            if (off + ANALOG_SLOT > data.length) break;
            if (!data[off]) continue;
            this._onChange('analog', pin, {
                enabled: true,
                isOutput: data[off + 1] !== 0,
                value: data[off + 2] | (data[off + 3] << 8),
            });
        }
    }

    _firePwmChanges(data) {
        if (!data) return;
        const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
        for (let pin = 0; pin < GPIO_MAX_PINS; pin++) {
            const off = pin * PWM_SLOT;
            if (off + PWM_SLOT > data.length) break;
            if (!data[off]) continue;
            const dutyCycle = view.getUint16(off + 2, true);
            this._onChange('pwm', pin, {
                enabled: true,
                variableFreq: data[off + 1] !== 0,
                dutyCycle,
                frequency: view.getUint32(off + 4, true),
                brightness: dutyCycle / 65535,
            });
        }
    }

    // ── I2C routing ──

    _i2cOnWrite(path, data) {
        const addr = this._parseI2cAddr(path);
        if (addr === null) return;
        const device = this._i2cDevices.get(addr);
        if (!device || !data || data.length === 0) return;
        const register = data[0];
        const payload = data.slice(1);
        for (let i = 0; i < payload.length; i++) {
            if (register + i < 256) device.registers[register + i] = payload[i];
        }
        device.onWrite(register, payload);
    }

    _parseI2cAddr(path) {
        const m = path.match(/\/hal\/i2c\/dev\/(\d+)/);
        return m ? parseInt(m[1], 10) : null;
    }

    // ── View objects (API compatibility) ──

    hardware(name) {
        if (!this._views) this._buildViews();
        return this._views[name] || null;
    }

    _buildViews() {
        const self = this;
        this._views = {
            gpio: {
                get name() { return 'gpio'; },
                getPin(n) { return self.getGpioPin(n); },
                get pins() { return self._readAllGpioPins(); },
                setInputValue(memfs, pin, val) { self.setGpioInput(pin, val); },
            },
            analog: {
                get name() { return 'analog'; },
                getPin(n) { return self.getAnalogPin(n); },
                get pins() { return self._readAllAnalogPins(); },
                setInputValue(memfs, pin, val) { self.setAnalogInput(pin, val); },
            },
            pwm: {
                get name() { return 'pwm'; },
                getPin(n) { return self.getPwmPin(n); },
                get pins() { return self._readAllPwmPins(); },
                getBrightness(n) { const p = self.getPwmPin(n); return p ? p.brightness : 0; },
            },
            neopixel: {
                get name() { return 'neopixel'; },
                getStrip(pin) { return self.getNeopixelStrip(pin); },
                get strips() { return self._readAllNeopixelStrips(); },
            },
            i2c: {
                get name() { return 'i2c'; },
                addDevice(addr, dev) { self.addDevice(addr, dev); },
                removeDevice(addr) { self.removeDevice(addr); },
                getDevice(addr) { return self.getDevice(addr); },
                get devices() { return self._i2cDevices; },
                seedDeviceFiles(memfs) { self.seedDeviceFiles(memfs); },
            },
        };
    }
}
