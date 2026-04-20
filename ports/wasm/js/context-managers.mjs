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
 * Phase 2: independent timing per manager.
 *   - VMContext only steps when executing (drives cp_step which includes cp_hw_step)
 *   - DisplayContext always runs when display exists (calls cp_hw_step when VM idle)
 *   - HardwareContext syncs on VM activity or pending changes
 *   - IOContext polls connected devices independently
 *   - Visibility API: onVisible/onHidden for throttling
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
        this._visible = true;
    }

    /** Does this domain have pending work? */
    get needsWork() { return false; }

    /** Do one unit of work. */
    step(nowMs) {}

    /** Something happened in this domain. */
    wake(event) {}

    /** Clean state for this domain (Layer 3 teardown). */
    reset() {}

    /** Tab became visible — resume full-rate work. */
    onVisible() { this._visible = true; }

    /** Tab became hidden — throttle or pause. */
    onHidden() { this._visible = false; }
}

/**
 * VMContext — owns Python execution.
 *
 * Drives cp_step (which includes cp_hw_step internally) when VM is active.
 * When VM is READY, does nothing — DisplayContext handles VH updates.
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

        // cp_step drives both VM and VH when executing
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
            if (!m || m.status !== 6) continue;

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
 * Always runs when a display exists. When the VM is idle, calls
 * cp_hw_step directly to keep the display framebuffer fresh
 * (background callbacks, terminal refresh, cursor info update).
 * When the VM is executing, cp_step already calls cp_hw_step.
 *
 * Cursor blink runs on its own setInterval, independent of rAF.
 */
export class DisplayContext extends ContextManager {
    constructor(board) {
        super(board);
        this._cursorVisible = false;
        this._cursorTimer = null;
        this._cursorBlinkMs = 530;
        this._showCursor = false;
    }

    get needsWork() {
        return !!this._board._display;
    }

    step(nowMs) {
        const board = this._board;
        if (!board._display) return;

        // When VM is idle, we need to drive cp_hw_step ourselves
        // so the display framebuffer stays fresh (terminal, cursor info).
        // When VM is executing, cp_step already called cp_hw_step.
        const st = board._exports?.cp_state?.() ?? CP_STATE_READY;
        if (st === CP_STATE_READY) {
            board._exports.cp_hw_step(nowMs);
        }

        board._display.paint();
        if (this._showCursor && this._cursorVisible) {
            board._display.drawCursor();
        }
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
 * HardwareContext — owns pin/neopixel/analog/I2C state.
 *
 * Runs hardware module preStep/postStep hooks. Needs work when VM
 * is active (state is being produced), when a final sync is pending
 * (VM just finished), or when a display exists (continuous rendering).
 */
export class HardwareContext extends ContextManager {
    constructor(board) {
        super(board);
        this._pendingSync = false;
    }

    get needsWork() {
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
 * Polls connected devices at a reduced rate (~3/sec).
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
        // Reduce polling rate when hidden
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
