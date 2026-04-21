/**
 * context-managers.mjs — Output consumers for CircuitPython WASM.
 *
 * wasm_frame() handles all C-side work (port → supervisor → VM).
 * These managers consume the output: painting the display, syncing
 * hardware modules, polling external devices.
 *
 * They do NOT call C exports (no cp_step, cp_hw_step). They only
 * read state from linear memory and update JS-side resources.
 *
 * Protocol:
 *   reset()      — clean state for this domain (Layer 3 teardown)
 *   onVisible()  — tab became visible
 *   onHidden()   — tab became hidden
 */

/**
 * Base class — defines the protocol.
 */
export class ContextManager {
    constructor(board) {
        /** @type {import('./circuitpython.mjs').CircuitPython} */
        this._board = board;
        this._visible = true;
    }

    /** Clean state for this domain (Layer 3 teardown). */
    reset() {}

    /** Tab became visible — resume full-rate work. */
    onVisible() { this._visible = true; }

    /** Tab became hidden — throttle or pause. */
    onHidden() { this._visible = false; }
}

/**
 * DisplayContext — owns cursor blink state.
 *
 * Display painting (paint + drawCursor) is called directly by _loop
 * after wasm_frame. This manager only owns the cursor blink timer
 * and visibility state.
 */
export class DisplayContext extends ContextManager {
    constructor(board) {
        super(board);
        this._cursorVisible = false;
        this._cursorTimer = null;
        this._cursorBlinkMs = 530;
        this._showCursor = false;
    }

    startCursorBlink() {
        this._showCursor = true;
        this._cursorVisible = true;
        if (this._cursorTimer) clearInterval(this._cursorTimer);
        this._cursorTimer = setInterval(() => {
            this._cursorVisible = !this._cursorVisible;
        }, this._cursorBlinkMs);
    }

    stopCursorBlink() {
        this._showCursor = false;
        this._cursorVisible = false;
        if (this._cursorTimer) {
            clearInterval(this._cursorTimer);
            this._cursorTimer = null;
        }
    }

    onHidden() {
        super.onHidden();
        this.stopCursorBlink();
    }

    reset() {
        this.stopCursorBlink();
    }
}

/**
 * HardwareContext — owns JS-side hardware module sync.
 *
 * preStep/postStep are called directly by _loop after wasm_frame.
 * This manager owns the reset lifecycle for hardware modules.
 */
export class HardwareContext extends ContextManager {
    reset() {
        const board = this._board;
        for (const mod of board._hw._modules) {
            if (mod.reset) mod.reset(board._wasi);
        }
    }
}

/**
 * IOContext — owns external hardware targets (WebUSB, WebSerial).
 *
 * Polls connected devices at a reduced rate. Called directly by _loop.
 */
export class IOContext extends ContextManager {
    constructor(board) {
        super(board);
        this._pollCounter = 0;
        this._pollInterval = 20;
    }

    get needsWork() {
        return !!(this._board._target && this._board._target.connected);
    }

    step(nowMs) {
        const target = this._board._target;
        if (!target || !target.connected) return;

        target.applyState(this._board._wasi, nowMs);

        if (++this._pollCounter >= this._pollInterval) {
            this._pollCounter = 0;
            target.pollInputs();
        }
    }

    onHidden() {
        super.onHidden();
        this._pollInterval = 60;
    }

    onVisible() {
        super.onVisible();
        this._pollInterval = 20;
    }

    reset() {
        this._pollCounter = 0;
    }
}
