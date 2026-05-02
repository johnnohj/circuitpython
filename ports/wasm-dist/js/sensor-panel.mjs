/**
 * sensor-panel.mjs — sensor control UI for the sync bus
 *
 * Reads component definitions (boards/components/{name}/definition.json)
 * and creates slider + waveform controls that write sensor values
 * to sync bus analog/sensor slots.
 *
 * Each sensor has:
 *   - A slider (manual value)
 *   - A waveform mode (sine, triangle, square, noise)
 *   - A period control (when using waveforms)
 *   - A live readout
 *
 * Values are written to the sync bus analog slots AND to the VM
 * Worker so the Python common-hal can read them.
 */

// ── Waveform generators ──

function sine(t, period)     { return Math.sin(2 * Math.PI * t / period) * 0.5 + 0.5; }
function triangle(t, period) { return 2 * Math.abs(2 * (t / period - Math.floor(t / period + 0.5))) - 1; }
function square(t, period)   { return (t % period) < period / 2 ? 1 : 0; }
function sawtooth(t, period) { return (t % period) / period; }
function noise()             { return Math.random(); }

const WAVEFORMS = { none: null, sine, triangle, square, sawtooth, noise };

// ── Sensor state ──

class SensorControl {
    constructor(def, index) {
        this.def = def;           // from component definition sensors[]
        this.index = index;       // analog slot index
        this.min = def.range?.min ?? 0;
        this.max = def.range?.max ?? 100;
        this.value = def.defaultValue ?? (this.min + this.max) / 2;
        this.waveform = 'none';
        this.period = def.defaultPeriod ?? 2;  // seconds
        this.startTime = performance.now() / 1000;
        this.el = null;           // DOM element
        this.slider = null;
        this.readout = null;
    }

    /** Compute current value (manual or waveform) */
    tick() {
        const fn = WAVEFORMS[this.waveform];
        if (!fn) return this.value;

        const t = performance.now() / 1000 - this.startTime;
        const normalized = this.waveform === 'noise' ? fn() : fn(t, this.period);
        this.value = this.min + normalized * (this.max - this.min);
        if (this.slider) this.slider.value = this.value;
        return this.value;
    }

    /** Convert float value to uint16 for analog slot (0–65535 range) */
    toAnalogU16() {
        const normalized = (this.value - this.min) / (this.max - this.min);
        return Math.round(Math.max(0, Math.min(1, normalized)) * 65535);
    }
}


// ── Panel builder ──

export class SensorPanel {
    constructor(container, options = {}) {
        this._container = container;
        this._sensors = [];       // SensorControl[]
        this._onUpdate = options.onUpdate || null;  // (index, value, u16) => void
        this._nextSlot = 0;
    }

    /**
     * Add a component's sensors to the panel.
     * @param {object} componentDef — from boards/components/{name}/definition.json
     */
    addComponent(componentDef) {
        const group = document.createElement('div');
        group.className = 'sensor-group';

        const title = document.createElement('div');
        title.className = 'sensor-group-title';
        title.textContent = componentDef.displayName || componentDef.componentName;
        group.appendChild(title);

        for (const sensorDef of (componentDef.sensors || [])) {
            const ctrl = new SensorControl(sensorDef, this._nextSlot++);
            ctrl.el = this._buildControl(ctrl);
            group.appendChild(ctrl.el);
            this._sensors.push(ctrl);
        }

        this._container.appendChild(group);
    }

    /**
     * Add a raw analog pin as a controllable sensor.
     * @param {string} name — pin name (e.g. "A0")
     * @param {object} [opts] — range, default, unit
     */
    addAnalogPin(name, opts = {}) {
        const sensorDef = {
            type: 'analog',
            displayName: name,
            unit: opts.unit || 'V',
            range: { min: opts.min ?? 0, max: opts.max ?? 3.3 },
            defaultValue: opts.defaultValue ?? 0,
            defaultPeriod: 2,
        };
        const ctrl = new SensorControl(sensorDef, this._nextSlot++);
        ctrl.el = this._buildControl(ctrl);
        this._container.appendChild(ctrl.el);
        this._sensors.push(ctrl);
    }

    /** Tick all sensors (call per frame). Returns array of { index, value, u16 } */
    tick() {
        const updates = [];
        for (const ctrl of this._sensors) {
            const value = ctrl.tick();
            const u16 = ctrl.toAnalogU16();
            if (ctrl.readout) {
                ctrl.readout.textContent = `${value.toFixed(2)} ${ctrl.def.unit || ''}`;
            }
            updates.push({ index: ctrl.index, value, u16 });
            if (this._onUpdate) this._onUpdate(ctrl.index, value, u16);
        }
        return updates;
    }

    /** Build DOM for one sensor control */
    _buildControl(ctrl) {
        const row = document.createElement('div');
        row.className = 'sensor-row';

        // Label
        const label = document.createElement('label');
        label.className = 'sensor-label';
        label.textContent = ctrl.def.displayName || ctrl.def.type;
        row.appendChild(label);

        // Slider
        const slider = document.createElement('input');
        slider.type = 'range';
        slider.className = 'sensor-slider';
        slider.min = ctrl.min;
        slider.max = ctrl.max;
        slider.step = (ctrl.max - ctrl.min) / 1000;
        slider.value = ctrl.value;
        slider.addEventListener('input', () => {
            ctrl.value = parseFloat(slider.value);
            ctrl.waveform = 'none';  // manual override stops waveform
            waveSelect.value = 'none';
        });
        ctrl.slider = slider;
        row.appendChild(slider);

        // Readout
        const readout = document.createElement('span');
        readout.className = 'sensor-readout';
        readout.textContent = `${ctrl.value.toFixed(2)} ${ctrl.def.unit || ''}`;
        ctrl.readout = readout;
        row.appendChild(readout);

        // Waveform select
        const waveSelect = document.createElement('select');
        waveSelect.className = 'sensor-wave';
        for (const name of Object.keys(WAVEFORMS)) {
            const opt = document.createElement('option');
            opt.value = name;
            opt.textContent = name === 'none' ? '—' : name;
            waveSelect.appendChild(opt);
        }
        waveSelect.value = ctrl.waveform;
        waveSelect.addEventListener('change', () => {
            ctrl.waveform = waveSelect.value;
            ctrl.startTime = performance.now() / 1000;
        });
        row.appendChild(waveSelect);

        // Period input (seconds)
        const periodInput = document.createElement('input');
        periodInput.type = 'number';
        periodInput.className = 'sensor-period';
        periodInput.min = 0.1;
        periodInput.max = 60;
        periodInput.step = 0.1;
        periodInput.value = ctrl.period;
        periodInput.title = 'Period (seconds)';
        periodInput.addEventListener('input', () => {
            ctrl.period = parseFloat(periodInput.value) || 1;
        });
        row.appendChild(periodInput);

        return row;
    }
}
