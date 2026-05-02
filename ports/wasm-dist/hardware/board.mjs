/**
 * board.mjs — unified hardware renderer wrapper
 *
 * Wraps board.wasm (Emscripten SDL2 build). Handles:
 *   - Definition.json parsing → C layout calls
 *   - State updates from parent (GPIO, NeoPixel, framebuffer)
 *   - Mouse interaction → hit test → parent notifications
 *   - Message protocol (extensible toward Wippersnapper Protobuf)
 *
 * Runs inside an <iframe>. Parent sends state, receives interactions.
 */

let Module = null;
let canvas = null;
let boardDef = null;
let pinMap = {};
let debounceTimers = new Map();
const DEBOUNCE_MS = 150;

// ── Init ──

export async function init(canvasElement, options = {}) {
    canvas = canvasElement;

    const createModule = (await import('./board.js')).default;
    Module = await createModule({
        canvas: canvasElement,
        noInitialRun: true,
    });

    if (options.definitionUrl) {
        boardDef = await fetch(options.definitionUrl).then(r => r.json());
    } else if (options.definition) {
        boardDef = options.definition;
    }

    const vis = boardDef?.visual || {};
    const width = vis.width || 400;
    const height = vis.height || 220;
    const bg = boardDef?.background || { r: 30, g: 30, b: 50 };

    Module._board_init(width, height, bg.r, bg.g, bg.b);
    canvasElement.width = width;
    canvasElement.height = height;

    if (boardDef) applyDefinition(boardDef);
    setupMouse(canvasElement);
    requestAnimationFrame(renderLoop);

    return { Module, boardDef, pinMap };
}


// ── Definition → C layout ──

function applyDefinition(def) {
    const pins = def.pins || [];
    pins.forEach((pin) => {
        const index = pin.id ?? 0;
        const vis = pin.visual || {};
        const namePtr = allocStr(pin.name || `P${index}`);
        Module._board_add_pin(index, vis.x || 0, vis.y || 0,
            vis.radius || 8, inferCategory(pin), namePtr);
        Module._free(namePtr);
        pinMap[pin.name] = index;
    });

    let neoIdx = 0;
    pins.forEach((pin) => {
        if (pin.capabilities?.neopixel) {
            const vis = pin.visual || {};
            const count = pin.neopixelCount || 10;
            for (let i = 0; i < count; i++) {
                Module._board_add_neopixel(neoIdx + i,
                    (vis.x || 200) + i * 16,
                    (vis.y || 100) + 20, 6);
            }
            neoIdx += count;
        }
    });
}

function inferCategory(pin) {
    const cap = pin.capabilities || {};
    if (cap.button) return 5;
    if (cap.led) return 6;
    if (cap.neopixel) return 7;
    if (cap.analog || cap.analog_in) return 2;
    if (cap.i2c_sda || cap.i2c_scl) return 3;
    if (cap.spi_sck || cap.spi_mosi || cap.spi_miso) return 4;
    return 1;
}

function allocStr(s) {
    const bytes = new TextEncoder().encode(s + '\0');
    const ptr = Module._malloc(bytes.length);
    Module.HEAPU8.set(bytes, ptr);
    return ptr;
}


// ── State updates (from parent postMessage) ──

export function updateGpio(data) {
    const ptr = Module._malloc(data.length);
    Module.HEAPU8.set(data, ptr);
    Module._board_update_gpio(ptr, data.length);
    Module._free(ptr);
}

export function updateNeopixel(data) {
    const ptr = Module._malloc(data.length);
    Module.HEAPU8.set(data, ptr);
    Module._board_update_neopixel(ptr, data.length);
    Module._free(ptr);
}

export function updateFramebuffer(data) {
    const ptr = Module._malloc(data.length);
    Module.HEAPU8.set(data, ptr);
    Module._board_update_framebuffer(ptr, data.length);
    Module._free(ptr);
}

export function resetState() {
    Module._board_reset_state();
}


// ── Mouse interaction ──

function setupMouse(el) {
    el.addEventListener('mousemove', (e) => {
        const rect = el.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        const pin = Module._board_hit_test(x, y);
        Module._board_set_hover(pin);
        postToParent({
            type: 'hover', pin: pin >= 0 ? pin : null,
            name: pin >= 0 ? Module.UTF8ToString(Module._board_get_pin_name(pin)) : null,
            category: pin >= 0 ? Module._board_get_pin_category(pin) : null,
            x, y,
        });
    });

    el.addEventListener('mousedown', (e) => {
        const rect = el.getBoundingClientRect();
        const x = e.clientX - rect.left, y = e.clientY - rect.top;
        const pin = Module._board_hit_test(x, y);
        if (pin >= 0) {
            Module._board_set_pressed(pin, 1);
            const timer = debounceTimers.get(pin);
            if (timer) clearTimeout(timer);
            postToParent({
                type: 'press', pin,
                name: Module.UTF8ToString(Module._board_get_pin_name(pin)),
                value: 0, x, y,
            });
        }
    });

    el.addEventListener('mouseup', (e) => {
        const rect = el.getBoundingClientRect();
        const x = e.clientX - rect.left, y = e.clientY - rect.top;
        const pin = Module._board_hit_test(x, y);
        for (let i = 0; i < 64; i++) Module._board_set_pressed(i, 0);
        if (pin >= 0) {
            debounceTimers.set(pin, setTimeout(() => {
                postToParent({
                    type: 'release', pin,
                    name: Module.UTF8ToString(Module._board_get_pin_name(pin)),
                    value: 1, x, y,
                });
                debounceTimers.delete(pin);
            }, DEBOUNCE_MS));
        }
    });

    el.addEventListener('mouseleave', () => {
        Module._board_set_hover(-1);
        for (let i = 0; i < 64; i++) Module._board_set_pressed(i, 0);
        postToParent({ type: 'hover', pin: null });
    });
}


// ── Render loop ──

function renderLoop() {
    Module._board_render();
    requestAnimationFrame(renderLoop);
}


// ── Parent communication ──

function postToParent(msg) {
    if (window.parent !== window) {
        window.parent.postMessage({ source: 'hardware', ...msg }, '*');
    }
}

window.addEventListener('message', (e) => {
    const msg = e.data;
    if (!msg || msg.source === 'hardware') return;

    switch (msg.type) {
        case 'gpio':
            if (msg.data) updateGpio(new Uint8Array(msg.data));
            break;
        case 'neopixel':
            if (msg.data) updateNeopixel(new Uint8Array(msg.data));
            break;
        case 'framebuffer':
            if (msg.data) updateFramebuffer(new Uint8Array(msg.data));
            break;
        case 'reset':
            resetState();
            break;
    }
});
