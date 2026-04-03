/**
 * display.mjs — Framebuffer rendering + cursor overlay for CircuitPython WASM.
 *
 * Reads the RGB565 framebuffer and cursor info struct from WASM linear memory
 * and paints to a canvas.  No-op if no canvas provided (Node.js mode).
 */

const CURSOR_BLINK_MS = 500;

export class Display {
    /**
     * @param {HTMLCanvasElement|null} canvas
     * @param {WebAssembly.Exports} exports
     */
    constructor(canvas, exports) {
        this._exports = exports;
        this._canvas = canvas;
        this._ctx = canvas ? canvas.getContext('2d') : null;

        // Framebuffer info — read once at init (static addresses)
        this._fbAddr = exports.wasm_display_fb_addr();
        this._fbWidth = exports.wasm_display_fb_width();
        this._fbHeight = exports.wasm_display_fb_height();
        this._cursorInfoAddr = exports.wasm_cursor_info_addr();

        this._imageData = this._ctx
            ? this._ctx.createImageData(this._fbWidth, this._fbHeight)
            : null;

        // Scale canvas to match displayio
        if (canvas) {
            canvas.style.width = this._fbWidth + 'px';
            canvas.style.height = this._fbHeight + 'px';
        }

        // Cursor blink state
        this._cursorOn = true;
        this._cursorLastToggle = performance.now();
    }

    get width() { return this._fbWidth; }
    get height() { return this._fbHeight; }

    /** Convert RGB565 framebuffer to RGBA and paint to canvas. */
    paint() {
        if (!this._ctx || !this._fbAddr || this._fbWidth === 0) return;

        const fb = new Uint16Array(
            this._exports.memory.buffer, this._fbAddr,
            this._fbWidth * this._fbHeight);
        const rgba = this._imageData.data;

        for (let i = 0; i < fb.length; i++) {
            const pixel = fb[i];
            const j = i * 4;
            rgba[j]     = ((pixel >> 11) & 0x1F) << 3;
            rgba[j + 1] = ((pixel >> 5) & 0x3F) << 2;
            rgba[j + 2] = (pixel & 0x1F) << 3;
            rgba[j + 3] = 255;
        }
        this._ctx.putImageData(this._imageData, 0, 0);
    }

    /**
     * Draw the blinking cursor overlay.
     * Reads cursor_info_t from WASM linear memory:
     *   0: cursor_x, 2: cursor_y, 4: scroll_x, 6: scroll_y,
     *   8: top_left_y, 10: height_tiles, 12: glyph_w, 14: glyph_h,
     *   16: scale
     */
    drawCursor() {
        if (!this._ctx) return;

        const now = performance.now();
        if (now - this._cursorLastToggle >= CURSOR_BLINK_MS) {
            this._cursorOn = !this._cursorOn;
            this._cursorLastToggle = now;
        }
        if (!this._cursorOn) return;

        const mem = this._exports.memory.buffer;
        const ci = new DataView(mem, this._cursorInfoAddr, 20);
        const cx = ci.getUint16(0, true);
        const cy = ci.getUint16(2, true);
        const sx = ci.getUint16(4, true);
        const sy = ci.getUint16(6, true);
        const tly = ci.getUint16(8, true);
        const htiles = ci.getUint16(10, true);
        const gw = ci.getUint16(12, true);
        const gh = ci.getUint16(14, true);
        const scale = ci.getUint16(16, true) || 1;

        if (gw === 0 || gh === 0 || htiles === 0) return;

        const visualRow = ((cy - tly) % htiles + htiles) % htiles;
        const px = (sx + cx * gw) * scale;
        const py = (sy + visualRow * gh) * scale;
        const w = gw * scale;
        const h = gh * scale;

        this._ctx.save();
        this._ctx.globalCompositeOperation = 'difference';
        this._ctx.fillStyle = '#ffffff';
        this._ctx.fillRect(px, py, w, h);
        this._ctx.restore();
    }
}
