/**
 * context-managers.mjs — Domain managers for CircuitPython WASM.
 *
 * Each manager owns a domain of the system and follows a uniform protocol:
 *
 *   needsWork   — does this domain have pending work?
 *   step(now)   — do one unit of work
 *   wake(event) — something happened in this domain
 *   reset()     — clean state for this domain
 *
 * Context managers submit events to the semihosting ring and read state
 * from MEMFS/exports. They don't call C exports directly (except through
 * the step protocol). circuitpython.mjs orchestrates time allocation
 * across managers.
 *
 * Phase 1: wraps existing code, same behavior, cleaner structure.
 */

// cp_state() return values
const CP_STATE_READY     = 0;
const CP_STATE_EXECUTING = 1;
const CP_STATE_SUSPENDED = 2;

/**
 * Base class — defines the protocol.
 */
export class ContextManager {
    constructor(board) {
        /** @type {import('./circuitpython.mjs').CircuitPython} */
        this._board = board;
    }

    /** Does this domain have pending work? */
    get needsWork() { return false; }

    /** Do one unit of work. */
    step(nowMs) {}

    /** Something happened in this domain. */
    wake(event) {}

    /** Clean state for this domain (Layer 3 teardown). */
    reset() {}
}

/**
 * VMContext — owns Python execution.
 *
 * Calls cp_step (which includes cp_hw_step internally for now).
 * Tracks state transitions, fires callbacks on completion.
 */
export class VMContext extends ContextManager {
    constructor(board) {
        super(board);
        this._prevState = CP_STATE_READY;
    }

    get needsWork() {
        const st = this._board._exports?.cp_state?.() ?? CP_STATE_READY;
        return st !== CP_STATE_READY;
    }

    step(nowMs) {
        const exports = this._board._exports;
        if (!exports) return;

        // cp_step drives VM + VH (backward compat: cp_step calls cp_hw_step)
        exports.cp_step(nowMs);

        // State transition detection
        const st = exports.cp_state();

        if (this._prevState > 0 && st === CP_STATE_READY) {
            // Execution just finished — notify hardware to do one more sync
            if (this._board._hwCtx) this._board._hwCtx._pendingSync = true;
            // Show prompt if readline is waiting for a result
            if (this._board._readline?.waitingForResult) {
                this._board._readline.onResult();
            }
            if (this._board._onCodeDone && !this._board._codeDoneFired && this._board._ctx0IsCode) {
                this._board._codeDoneFired = true;
                this._board._onCodeDone();
            }
        }
        this._prevState = st;

        // Background context lifecycle
        this._cleanupDoneContexts();
    }

    reset() {
        this._prevState = CP_STATE_READY;
    }

    _cleanupDoneContexts() {
        const board = this._board;
        for (let i = 1; i < board._ctxMax; i++) {
            const m = board._readContextMeta(i);
            if (!m || m.status !== 6) continue;  // only DONE

            const cb = board._ctxCallbacks.get(i);
            if (cb) {
                board._ctxCallbacks.delete(i);
                cb(i, null);
            }

            const active = board._exports.cp_context_active();
            if (active === i) {
                board._exports.cp_context_save(i);
                board._exports.cp_context_restore(0);
            }
            board._exports.cp_context_destroy(i);
        }
    }
}

/**
 * DisplayContext — owns the framebuffer, terminal, and cursor.
 *
 * Calls display.paint() each step. Manages cursor blink independently.
 */
export class DisplayContext extends ContextManager {
    constructor(board) {
        super(board);
        this._cursorVisible = false;
        this._cursorTimer = null;
        this._cursorBlinkMs = 530;
        this._showCursor = false;  // set by circuitpython.mjs based on mode
    }

    get needsWork() {
        // Display always needs work if it exists (framebuffer may have changed)
        return !!this._board._display;
    }

    step(nowMs) {
        if (this._board._display) {
            this._board._display.paint();
            if (this._showCursor && this._cursorVisible) {
                this._board._display.drawCursor();
            }
        }
    }

    /** Start blinking the cursor (REPL mode, focused). */
    startCursorBlink() {
        this._showCursor = true;
        this._cursorVisible = true;
        if (this._cursorTimer) clearInterval(this._cursorTimer);
        this._cursorTimer = setInterval(() => {
            this._cursorVisible = !this._cursorVisible;
        }, this._cursorBlinkMs);
    }

    /** Stop cursor blink (code running, or no focus). */
    stopCursorBlink() {
        this._showCursor = false;
        this._cursorVisible = false;
        if (this._cursorTimer) {
            clearInterval(this._cursorTimer);
            this._cursorTimer = null;
        }
    }

    reset() {
        this.stopCursorBlink();
    }
}

/**
 * HardwareContext — owns pin/neopixel/analog/I2C state.
 *
 * Runs hardware module preStep/postStep hooks each frame.
 */
export class HardwareContext extends ContextManager {
    constructor(board) {
        super(board);
        this._pendingSync = false;
    }

    get needsWork() {
        // Hardware needs syncing when:
        // - VM is active (executing/suspended)
        // - A final sync is pending (VM just finished)
        // - A display exists (display needs continuous hw state for rendering)
        if (this._pendingSync) return true;
        if (this._board._display) return true;
        const st = this._board._exports?.cp_state?.() ?? CP_STATE_READY;
        return st !== CP_STATE_READY;
    }

    step(nowMs) {
        const board = this._board;
        board._hw.preStep(board._wasi, nowMs);
        board._hw.postStep(board._wasi, nowMs);
        this._pendingSync = false;
    }

    reset() {
        this._pendingSync = false;
        const board = this._board;
        for (const mod of board._hw._modules) {
            if (mod.reset) mod.reset(board._wasi);
        }
    }
}

/**
 * IOContext — owns external hardware targets (WebUSB, WebSerial).
 *
 * Polls connected devices at a reduced rate.
 */
export class IOContext extends ContextManager {
    constructor(board) {
        super(board);
        this._pollCounter = 0;
        this._pollInterval = 20;  // every 20 frames (~3/sec at 60fps)
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

    reset() {
        this._pollCounter = 0;
    }
}
