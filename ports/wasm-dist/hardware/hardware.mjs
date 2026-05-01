/**
 * hardware.mjs — Emscripten hardware renderer wrapper
 *
 * Wraps the SDL2-based C renderer. Handles:
 *   - Board definition parsing (JSON → C layout calls)
 *   - Sync bus reads → C state updates → render
 *   - Mouse interaction → hit test → hover/click events
 *   - Communication with parent page (postMessage)
 *
 * Runs inside an <iframe>. The parent page sends state updates
 * and receives interaction events.
 */

// ── Module state ──

let Module = null;      // Emscripten module
let canvas = null;
let boardDef = null;    // parsed definition.json
let pinMap = {};        // name → index
let debounceTimers = new Map();  // pin → timeout for click debounce

const DEBOUNCE_MS = 150;  // hold clicks this long minimum

// ── Init ──

export async function init(canvasElement, options = {}) {
    canvas = canvasElement;

    // Load Emscripten module
    const createModule = (await import('./renderer.js')).default;
    Module = await createModule({
        canvas: canvasElement,
        // Prevent Emscripten from auto-running main
        noInitialRun: true,
    });

    // Load board definition
    if (options.definitionUrl) {
        const resp = await fetch(options.definitionUrl);
        boardDef = await resp.json();
    } else if (options.definition) {
        boardDef = options.definition;
    }

    // Initialize renderer
    const vis = boardDef?.visual || {};
    const width = vis.width || 400;
    const height = vis.height || 300;
    const bg = boardDef?.background || { r: 30, g: 30, b: 50 };

    Module._hw_renderer_init(width, height, bg.r, bg.g, bg.b);
    canvasElement.width = width;
    canvasElement.height = height;

    // Lay out board elements from definition
    if (boardDef) applyBoardDefinition(boardDef);

    // Set up mouse interaction
    setupMouseHandlers(canvasElement);

    // Start render loop
    requestAnimationFrame(renderLoop);

    return { Module, boardDef, pinMap };
}


// ── Board definition → C layout ──

function applyBoardDefinition(def) {
    // Pins — map from definition.json schema
    const pins = def.pins || [];
    pins.forEach((pin) => {
        const index = pin.id ?? 0;
        const vis = pin.visual || {};
        const namePtr = allocString(pin.name || `P${index}`);
        Module._hw_add_pin(
            index,
            vis.x || 0,
            vis.y || 0,
            vis.radius || 8,
            inferCategory(pin),
            namePtr
        );
        Module._free(namePtr);
        pinMap[pin.name] = index;
    });

    // NeoPixels — look for pins with neopixel capability
    let neoIdx = 0;
    pins.forEach((pin) => {
        if (pin.capabilities?.neopixel) {
            const vis = pin.visual || {};
            // Place NeoPixel strip starting near the pin
            const count = pin.neopixelCount || 10;
            for (let i = 0; i < count; i++) {
                Module._hw_add_neopixel(neoIdx + i,
                    (vis.x || 200) + i * 16,
                    (vis.y || 100) + 20,
                    6);
            }
            neoIdx += count;
        }
    });

    // Built-in display
    const vis = def.visual || {};
    if (vis.width && vis.height) {
        // Place display in the center of the board
        const dw = Math.min(vis.width - 40, 240);
        const dh = Math.min(vis.height - 60, 180);
        Module._hw_init_display(
            (vis.width - dw) / 2 | 0,
            40,
            dw, dh,
            240, 180  // framebuffer dimensions
        );
    }
}

function inferCategory(pin) {
    const cap = pin.capabilities || {};
    if (cap.button) return 5;    // CAT_BUTTON
    if (cap.led) return 6;       // CAT_LED
    if (cap.neopixel) return 7;  // CAT_NEOPIXEL
    if (cap.analog) return 2;    // CAT_ANALOG
    if (cap.i2c_sda || cap.i2c_scl) return 3;  // CAT_I2C
    if (cap.spi_sck || cap.spi_mosi || cap.spi_miso) return 4;  // CAT_SPI
    return 1;  // CAT_DIGITAL
}

function allocString(str) {
    const bytes = new TextEncoder().encode(str + '\0');
    const ptr = Module._malloc(bytes.length);
    Module.HEAPU8.set(bytes, ptr);
    return ptr;
}


// ── State updates (called from parent messages or sync bus) ──

export function applyGpioState(gpioData) {
    // gpioData: Uint8Array, 32 pins × 12 bytes
    for (let i = 0; i < 32; i++) {
        const off = i * 12;
        Module._hw_set_pin_state(
            i,
            gpioData[off],      // enabled
            gpioData[off + 1],  // direction
            gpioData[off + 2],  // value
            gpioData[off + 3]   // pull
        );
    }
}

export function applyNeopixelState(neoData) {
    // neoData: Uint8Array, header(4) + GRB pixels
    const enabled = neoData[1];
    const numBytes = neoData[2] | (neoData[3] << 8);
    const bpp = 3;
    const count = enabled ? Math.floor(numBytes / bpp) : 0;
    for (let i = 0; i < count; i++) {
        const base = 4 + i * bpp;
        const g = neoData[base], r = neoData[base + 1], b = neoData[base + 2];
        Module._hw_set_neopixel_rgb(i, r, g, b);
    }
}

export function applyFramebuffer(fbData) {
    // fbData: Uint8Array (RGB565 bytes)
    const ptr = Module._malloc(fbData.length);
    Module.HEAPU8.set(fbData, ptr);
    Module._hw_update_framebuffer(ptr, fbData.length);
    Module._free(ptr);
}


// ── Mouse interaction ──

function setupMouseHandlers(el) {
    el.addEventListener('mousemove', (e) => {
        const rect = el.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        const pin = Module._hw_hit_test(x, y);
        Module._hw_set_hover(pin);

        // Send hover info to parent for tooltips
        postToParent({
            type: 'hover',
            pin: pin >= 0 ? pin : null,
            name: pin >= 0 ? Module.UTF8ToString(Module._hw_get_pin_name(pin)) : null,
            category: pin >= 0 ? Module._hw_get_pin_category(pin) : null,
            x, y,
        });
    });

    el.addEventListener('mousedown', (e) => {
        const rect = el.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        const pin = Module._hw_hit_test(x, y);

        if (pin >= 0) {
            Module._hw_set_pressed(pin, 1);

            // Debounce: hold the press for at least DEBOUNCE_MS
            const timer = debounceTimers.get(pin);
            if (timer) clearTimeout(timer);

            postToParent({
                type: 'press',
                pin,
                name: Module.UTF8ToString(Module._hw_get_pin_name(pin)),
                value: 0,  // pressed = LOW (pull-up convention)
                x, y,
            });
        }
    });

    el.addEventListener('mouseup', (e) => {
        const rect = el.getBoundingClientRect();
        const x = e.clientX - rect.left;
        const y = e.clientY - rect.top;
        const pin = Module._hw_hit_test(x, y);

        // Release all pressed pins (in case mouse moved off)
        for (let i = 0; i < 64; i++) {
            Module._hw_set_pressed(i, 0);
        }

        if (pin >= 0) {
            // Debounce: delay the release
            debounceTimers.set(pin, setTimeout(() => {
                postToParent({
                    type: 'release',
                    pin,
                    name: Module.UTF8ToString(Module._hw_get_pin_name(pin)),
                    value: 1,  // released = HIGH
                    x, y,
                });
                debounceTimers.delete(pin);
            }, DEBOUNCE_MS));
        }
    });

    el.addEventListener('mouseleave', () => {
        Module._hw_set_hover(-1);
        for (let i = 0; i < 64; i++) {
            Module._hw_set_pressed(i, 0);
        }
        postToParent({ type: 'hover', pin: null });
    });
}


// ── Render loop ──

function renderLoop() {
    Module._hw_render();
    requestAnimationFrame(renderLoop);
}


// ── Parent communication ──

function postToParent(msg) {
    if (window.parent !== window) {
        window.parent.postMessage({ source: 'hardware', ...msg }, '*');
    }
}

// Receive state updates from parent
window.addEventListener('message', (e) => {
    const msg = e.data;
    if (!msg || msg.source === 'hardware') return;  // ignore own echoes

    switch (msg.type) {
        case 'gpio':
            if (msg.data) applyGpioState(new Uint8Array(msg.data));
            break;
        case 'neopixel':
            if (msg.data) applyNeopixelState(new Uint8Array(msg.data));
            break;
        case 'framebuffer':
            if (msg.data) applyFramebuffer(new Uint8Array(msg.data));
            break;
        case 'definition':
            if (msg.definition) {
                boardDef = msg.definition;
                applyBoardDefinition(boardDef);
            }
            break;
    }
});
