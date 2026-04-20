/**
 * readline.mjs — REPL input handling for CircuitPython WASM.
 *
 * Manages input line accumulation, multi-line editing, history, tab
 * completion, and shell command interception.  DOM-independent — the
 * keydown listener lives in the caller (circuitpython.mjs).
 */

import { tryShellCommand } from './shell.mjs';

export class Readline {
    /**
     * @param {WebAssembly.Exports} exports
     * @param {object} options
     * @param {import('./fwip.js').Fwip} options.fwip
     * @param {number} options.ctxMax
     * @param {function} options.readContextMeta
     * @param {function} [options.onExec] — called with (len) when readline
     *        wants to execute. If not provided, falls back to cp_exec(0, len).
     */
    constructor(exports, options = {}) {
        this._exports = exports;
        this._fwip = options.fwip;
        this._ctxMax = options.ctxMax || 8;
        this._readContextMeta = options.readContextMeta;
        this._onExec = options.onExec || null;

        this._inputBufAddr = exports.cp_input_buf_addr();
        this._inputBufSize = exports.cp_input_buf_size();

        this._line = '';
        this._lines = '';         // accumulated multi-line input
        this._history = [];
        this._historyIdx = -1;
        this._waitingForResult = true;  // true after cp_exec or at boot

        this.PS1 = '>>> ';
        this.PS2 = '... ';
    }

    get waitingForResult() { return this._waitingForResult; }

    /** Called when an expression finishes and the REPL should show a prompt. */
    onResult() {
        this._waitingForResult = false;
        this.showPrompt();
    }

    /** Called on Ctrl-C to reset input state. */
    handleInterrupt() {
        if (this._line || this._lines) {
            this.termWrite('^C\r\n');
            this._line = '';
            this._lines = '';
            this.showPrompt();
        }
    }

    /** Write text to the shared input buffer. Returns byte length written. */
    writeInputBuf(text) {
        const enc = new TextEncoder();
        const bytes = enc.encode(text);
        const len = Math.min(bytes.length, this._inputBufSize - 1);
        new Uint8Array(this._exports.memory.buffer, this._inputBufAddr, len)
            .set(bytes.subarray(0, len));
        return len;
    }

    /** Write text through C's mp_hal_stdout (appears on displayio + serial). */
    termWrite(text) {
        const len = this.writeInputBuf(text);
        if (len > 0) this._exports.cp_print(len);
    }

    showPrompt() {
        this.termWrite(this._lines ? this.PS2 : this.PS1);
    }

    /**
     * Handle a key event. Call this from the keydown listener.
     * @param {string} key — e.key value
     * @param {boolean} ctrl — e.ctrlKey
     * @param {boolean} meta — e.metaKey
     * @returns {boolean} true if the key was handled
     */
    handleKey(key, ctrl, meta) {
        const exports = this._exports;

        // Ctrl+C
        if (ctrl && key === 'c') {
            exports.cp_ctrl_c();
            this.handleInterrupt();
            return true;
        }

        // Ctrl+D
        if (ctrl && key === 'd') {
            if (!this._line && !this._lines) {
                exports.cp_ctrl_d();
                this.termWrite('\r\n');
                this._waitingForResult = true;
            }
            return true;
        }

        // Enter
        if (key === 'Enter') {
            this.termWrite('\r\n');

            // Shell command interception
            if (!this._lines && tryShellCommand(this._line, {
                exports,
                fwip: this._fwip,
                readline: this,
                ctxMax: this._ctxMax,
                readContextMeta: this._readContextMeta,
            })) {
                if (this._line.trim()) {
                    this._history.unshift(this._line.trimEnd());
                }
                this._line = '';
                this._historyIdx = -1;
                return true;
            }

            const fullInput = this._lines
                ? this._lines + this._line + '\n'
                : this._line;

            if (this._line === '' && this._lines) {
                // Empty line in multi-line → execute compound statement
                const len = this.writeInputBuf(fullInput);
                const ret = this._onExec
                    ? this._onExec(len)
                    : exports.cp_exec(0, len);
                if (ret === 0) {
                    this._waitingForResult = true;
                    if (fullInput.trim()) this._history.unshift(fullInput.trimEnd());
                } else {
                    this.showPrompt();
                }
                this._line = '';
                this._lines = '';
                this._historyIdx = -1;
                return true;
            }

            // Check if we need more input (compound statement)
            const len = this.writeInputBuf(fullInput);
            if (exports.cp_continue(len)) {
                this._lines = fullInput + '\n';
                this._line = '';
                this.showPrompt();
                return true;
            }

            // Complete expression — execute
            const ret = this._onExec
                    ? this._onExec(len)
                    : exports.cp_exec(0, len);
            if (ret === 0) {
                this._waitingForResult = true;
                if (fullInput.trim()) this._history.unshift(fullInput.trimEnd());
            } else {
                this.showPrompt();
            }
            this._line = '';
            this._lines = '';
            this._historyIdx = -1;
            return true;
        }

        // Backspace
        if (key === 'Backspace') {
            if (this._line.length > 0) {
                this._line = this._line.slice(0, -1);
                this.termWrite('\b \b');
            }
            return true;
        }

        // Tab
        if (key === 'Tab') {
            const full = (this._lines || '') + this._line;
            const len = this.writeInputBuf(full);
            const complLen = exports.cp_complete(len);
            if (complLen > 0) {
                const bytes = new Uint8Array(
                    exports.memory.buffer, this._inputBufAddr, len + complLen);
                const completed = new TextDecoder().decode(bytes);
                const added = completed.slice(full.length);
                this._line += added;
                this.termWrite(added);
            }
            return true;
        }

        // Arrow up
        if (key === 'ArrowUp') {
            if (this._historyIdx < this._history.length - 1) {
                while (this._line.length > 0) {
                    this.termWrite('\b \b');
                    this._line = this._line.slice(0, -1);
                }
                this._historyIdx++;
                this._line = this._history[this._historyIdx];
                this.termWrite(this._line);
            }
            return true;
        }

        // Arrow down
        if (key === 'ArrowDown') {
            while (this._line.length > 0) {
                this.termWrite('\b \b');
                this._line = this._line.slice(0, -1);
            }
            if (this._historyIdx > 0) {
                this._historyIdx--;
                this._line = this._history[this._historyIdx];
                this.termWrite(this._line);
            } else {
                this._historyIdx = -1;
                this._line = '';
            }
            return true;
        }

        // Printable character
        if (key.length === 1 && !ctrl && !meta) {
            this._line += key;
            this.termWrite(key);
            return true;
        }

        return false;
    }
}
