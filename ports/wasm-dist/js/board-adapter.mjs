/**
 * board-adapter.mjs — Translates between named pins and GPIO indices.
 *
 * Loads definition.json and magic.json at init time and provides:
 *   - Pin name → GPIO index resolution (and reverse)
 *   - board.getSnapshot() — structured state for visual renderers
 *   - board.setInput(name, value) — inject input by pin name
 *   - Board identity, visual layout, bus definitions, on-board components
 *
 * This is the single bridge between visual components (jacdac-ts,
 * MakeCode, custom HTML) and the hardware module system (hardware.mjs).
 * Visual components never need to know GPIO indices — they work with
 * pin names like "A4", "NEOPIXEL", "BUTTON_A".
 *
 * Usage:
 *   import { BoardAdapter } from './board-adapter.mjs';
 *
 *   const adapter = new BoardAdapter(definition, magic);
 *   // After board.create():
 *   const snap = adapter.getSnapshot(board);
 *   // snap.pins.A4 = { name: 'A4', id: 18, gpio: {...}, analog: {...} }
 *   // snap.neopixels.NEOPIXEL = { numPixels: 10, pixels: [...] }
 *
 *   adapter.setInput(board, 'BUTTON_A', true);   // digital
 *   adapter.setInput(board, 'A0', 32768);         // analog (0-65535)
 */

// ── Pin lookup tables ──

/**
 * Build bidirectional lookup maps from a definition.json pins array.
 * @param {Array} pins — definition.json pins array
 * @returns {{ byName: Map<string, object>, byId: Map<number, object> }}
 */
function buildPinMaps(pins) {
    const byName = new Map();
    const byId = new Map();

    for (const pin of pins) {
        byName.set(pin.name, pin);
        byId.set(pin.id, pin);
        if (pin.aliases) {
            for (const alias of pin.aliases) {
                byName.set(alias, pin);
            }
        }
    }

    return { byName, byId };
}

// ── BoardAdapter ──

export class BoardAdapter {
    /**
     * @param {object} definition — parsed definition.json
     * @param {object} [magic] — parsed magic.json (on-board components)
     */
    constructor(definition, magic = null) {
        this._def = definition;
        this._magic = magic;
        const { byName, byId } = buildPinMaps(definition.pins);
        this._byName = byName;
        this._byId = byId;
    }

    // ── Board identity ──

    get boardName() { return this._def.boardName; }
    get displayName() { return this._def.displayName; }
    get vendor() { return this._def.vendor; }
    get mcuName() { return this._def.mcuName; }

    // ── Layout (for visual renderers) ──

    /** Visual layout: { image, width, height, pinDistance } */
    get visual() { return this._def.visual; }

    /** All pin definitions with visual coordinates. */
    get pins() { return this._def.pins; }

    /** Power pin positions. */
    get power() { return this._def.power; }

    /** On-board components (LEDs, buttons, sensors from magic.json). */
    get components() { return this._magic?.components || []; }

    // ── Bus definitions ──

    get i2cBuses() { return this._def.i2c || []; }
    get spiBuses() { return this._def.spi || []; }
    get uartBuses() { return this._def.uart || []; }

    // ── Pin resolution ──

    /**
     * Resolve a pin name (or alias) to its definition.
     * @param {string} name — "A4", "SDA", "BUTTON_A", etc.
     * @returns {object|null} — { name, id, capabilities, visual, aliases? }
     */
    resolvePin(name) {
        return this._byName.get(name) || null;
    }

    /**
     * Resolve a GPIO index to its pin definition.
     * @param {number} id — GPIO index (0-63)
     * @returns {object|null}
     */
    resolvePinById(id) {
        return this._byId.get(id) || null;
    }

    /**
     * Get the GPIO index for a pin name.
     * @param {string} name
     * @returns {number|null}
     */
    pinId(name) {
        const pin = this._byName.get(name);
        return pin ? pin.id : null;
    }

    /**
     * Get all pin names (excluding aliases).
     * @returns {string[]}
     */
    get pinNames() {
        return this._def.pins.map(p => p.name);
    }

    // ── State snapshot ──

    /**
     * Snapshot the entire board's hardware state.
     *
     * Returns a structured object keyed by pin NAME (not GPIO index),
     * so visual renderers never need to know internal numbering.
     *
     * @param {CircuitPython} board — the board instance
     * @returns {{ pins: object, neopixels: object, components: Array }}
     */
    getSnapshot(board) {
        const gpio = board.hardware('gpio');
        const analog = board.hardware('analog');
        const pwm = board.hardware('pwm');
        const neopixel = board.hardware('neopixel');

        // Pin states keyed by canonical name
        const pins = {};
        for (const def of this._def.pins) {
            const id = def.id;
            pins[def.name] = {
                name: def.name,
                id,
                displayName: def.displayName || def.name,
                capabilities: def.capabilities,
                aliases: def.aliases || [],
                visual: def.visual,
                gpio: gpio ? gpio.getPin(id) : null,
                analog: analog ? analog.getPin(id) : null,
                pwm: pwm ? pwm.getPin(id) : null,
            };
        }

        // NeoPixel strips keyed by pin name
        const neopixels = {};
        if (neopixel) {
            for (const [gpioId, strip] of neopixel.strips) {
                const def = this._byId.get(gpioId);
                const name = def ? def.name : `GPIO${gpioId}`;
                neopixels[name] = strip;
            }
        }

        // On-board component state (enriched with live values)
        const components = (this._magic?.components || []).map(comp => {
            const enriched = { ...comp };
            if (comp.pin) {
                const pinDef = this._byName.get(comp.pin);
                if (pinDef) {
                    enriched.gpioId = pinDef.id;
                    enriched.gpio = gpio ? gpio.getPin(pinDef.id) : null;
                    enriched.analog = analog ? analog.getPin(pinDef.id) : null;
                    enriched.pwm = pwm ? pwm.getPin(pinDef.id) : null;
                }
            }
            if (comp.type === 'neopixel' && comp.pin) {
                const pinDef = this._byName.get(comp.pin);
                if (pinDef && neopixel) {
                    enriched.strip = neopixel.getStrip(pinDef.id);
                }
            }
            return enriched;
        });

        return { pins, neopixels, components };
    }

    // ── Input injection ──

    /**
     * Set an input value by pin name.
     *
     * For digital pins (buttons): pass a boolean.
     * For analog pins (sensors, pots): pass a number 0-65535.
     *
     * @param {CircuitPython} board — the board instance
     * @param {string} pinName — "BUTTON_A", "A0", etc.
     * @param {boolean|number} value
     */
    setInput(board, pinName, value) {
        const def = this._byName.get(pinName);
        if (!def) return;

        const memfs = board._wasi;
        const id = def.id;

        if (typeof value === 'boolean') {
            const gpio = board.hardware('gpio');
            if (gpio) gpio.setInputValue(memfs, id, value);
        } else if (typeof value === 'number') {
            const analog = board.hardware('analog');
            if (analog) {
                analog.setInputValue(memfs, id, Math.max(0, Math.min(65535, value | 0)));
            }
        }
    }
}

// ── Factory ──

/**
 * Load a BoardAdapter from JSON files (fetch or import).
 *
 * @param {string} definitionUrl — URL or path to definition.json
 * @param {string} [magicUrl] — URL or path to magic.json
 * @returns {Promise<BoardAdapter>}
 */
export async function loadBoardAdapter(definitionUrl, magicUrl) {
    const isNode = typeof process !== 'undefined' && process.versions?.node;
    const fetchJson = async (url) => {
        if (isNode) {
            // Node.js: use fs.readFile (fetch doesn't accept relative paths)
            const { readFile } = await import('node:fs/promises');
            return JSON.parse(await readFile(url, 'utf-8'));
        }
        const resp = await fetch(url);
        return resp.json();
    };

    const definition = await fetchJson(definitionUrl);
    const magic = magicUrl ? await fetchJson(magicUrl) : null;
    return new BoardAdapter(definition, magic);
}
