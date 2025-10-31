// JavaScript library for CircuitPython WASM - NeoPixel Module
// Provides access to NeoPixel LED state for visualization

mergeInto(LibraryManager.library, {
    // Placeholder function for initialization
    mp_js_neopixel: () => {},

    mp_js_neopixel__postset: `
        // NeoPixel state structure:
        // For each pin (0-63):
        //   - pixels[MAX_LEDS_PER_PIN * 3]: RGB data (3 bytes per pixel, max 256 LEDs)
        //   - num_bytes (uint32_t): Number of bytes in pixel buffer
        //   - enabled (bool): Whether this pin has NeoPixels
        //
        // Structure size per pin: 256*3 + 4 + 1 = 773 bytes (padded to 776 for alignment)

        var neopixelStatePtr = null;
        var MAX_LEDS_PER_PIN = 256;
        var NEOPIXEL_STATE_SIZE = 776;  // Per-pin state size (aligned)

        function initNeoPixelState() {
            if (neopixelStatePtr === null) {
                neopixelStatePtr = Module.ccall('get_neopixel_state_ptr', 'number', [], []);
            }
        }

        function getNeoPixelPinView(pin) {
            if (neopixelStatePtr === null) initNeoPixelState();
            var offset = neopixelStatePtr + (pin * NEOPIXEL_STATE_SIZE);
            return new DataView(Module.HEAPU8.buffer, offset, NEOPIXEL_STATE_SIZE);
        }

        // Export for external JavaScript visualization
        Module.getNeoPixelData = function(pin) {
            initNeoPixelState();
            var view = getNeoPixelPinView(pin);

            // Read enabled flag (at offset pixels[768] + num_bytes[4] = 772)
            var enabled = view.getUint8(772);
            if (!enabled) {
                return null;
            }

            // Read num_bytes (at offset 768)
            var numBytes = view.getUint32(768, true);
            if (numBytes === 0) {
                return null;
            }

            // Read pixel data
            var pixels = new Uint8Array(numBytes);
            for (var i = 0; i < numBytes; i++) {
                pixels[i] = view.getUint8(i);
            }

            return {
                numPixels: Math.floor(numBytes / 3),
                pixels: pixels,
                rgb: function(index) {
                    var offset = index * 3;
                    return {
                        r: pixels[offset],
                        g: pixels[offset + 1],
                        b: pixels[offset + 2]
                    };
                }
            };
        };
    `,

    // Get number of LEDs on a pin
    mp_js_neopixel_get_count: (pin) => {
        if (typeof Module.getNeoPixelData !== 'function') return 0;
        var data = Module.getNeoPixelData(pin);
        return data ? data.numPixels : 0;
    },

    // Get RGB color for a specific LED
    mp_js_neopixel_get_pixel: (pin, index, rgbPtr) => {
        if (typeof Module.getNeoPixelData !== 'function') return;
        var data = Module.getNeoPixelData(pin);
        if (!data || index >= data.numPixels) return;

        var color = data.rgb(index);
        Module.HEAPU8[rgbPtr] = color.r;
        Module.HEAPU8[rgbPtr + 1] = color.g;
        Module.HEAPU8[rgbPtr + 2] = color.b;
    },
});
