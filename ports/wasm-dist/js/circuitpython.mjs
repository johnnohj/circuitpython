/**
 * circuitpython.mjs — CircuitPython WASM board runtime.
 *
 * Drop-in ES module that loads a CircuitPython WASM binary, sets up the
 * WASI runtime, displayio canvas, REPL, and frame loop.  Works in
 * browser (canvas in a div) and Node.js (stdout callbacks, no canvas).
 *
 * Usage:
 *   import { CircuitPython } from './js/circuitpython.mjs';
 *
 *   const board = await CircuitPython.create({
 *       wasmUrl: 'build-browser/circuitpython.wasm',
 *       canvas: document.getElementById('display'),
 *       onStdout: (text) => terminal.write(text),
 *   });
 *
 *   board.exec('print(1+1)');
 *   board.ctrlC();
 *   board.ctrlD();
 */

import { WasiMemfs, IdbBackend, seedDrive } from './wasi-memfs.js';
import { Semihosting } from './semihosting.js';
import { getJsffiImports, jsffi_init } from './jsffi.js';
import { env } from './env.js';
import { Fwip } from './fwip.js';
import { Display } from './display.mjs';
import { Readline } from './readline.mjs';

const ANSI_RE = /\x1b\[[0-9;]*[A-Za-z]|\x1b\][^\x07]*\x07|\x1b\[[\?]?[0-9;]*[hlm]/g;

const SUP_REPL = 1;
const SUP_STATES = ['INIT', 'REPL', 'EXPR', 'CODE', 'DONE'];
const YIELD_REASONS = ['budget', 'sleep', 'show', 'io_wait', 'stdin'];
const CTX_STATUSES = ['FREE', 'IDLE', 'RUNNABLE', 'RUNNING', 'YIELDED', 'SLEEPING', 'DONE'];

export class CircuitPython {
    /**
     * Create and boot a CircuitPython board.
     *
     * @param {object} options
     * @param {string} options.wasmUrl — URL or path to circuitpython.wasm
     * @param {HTMLCanvasElement} [options.canvas] — displayio canvas (browser only)
     * @param {HTMLElement} [options.statusEl] — status bar element (browser only)
     * @param {HTMLElement} [options.serialEl] — serial output element (browser only)
     * @param {function} [options.onStdout] — stdout callback (text)
     * @param {function} [options.onStderr] — stderr callback (text)
     * @param {string} [options.codePy] — seed code.py content
     * @param {boolean} [options.persist] — enable IndexedDB persistence
     * @param {object} [options.files] — additional files to seed: { path: content }
     * @param {function} [options.onCodeDone] — called when code.py finishes (before REPL)
     * @returns {Promise<CircuitPython>}
     */
    static async create(options = {}) {
        const board = new CircuitPython();
        await board._init(options);
        return board;
    }

    constructor() {
        this._exports = null;
        this._wasi = null;
        this._sh = null;
        this._display = null;
        this._readline = null;
        this._fwip = null;
        this._raf = null;
        this._frameCount = 0;
        this._statusEl = null;
        this._serialEl = null;
        this._serialText = '';
        this._onStdout = null;
        this._onStderr = null;
        this._ctxMax = 0;
        this._ctxMetaSize = 0;
        this._visibilityHandler = null;
        this._keyHandler = null;
        this._stdinHandler = null;
        this._onCodeDone = null;
        this._codeDoneFired = false;
    }

    // ── Public API ──

    /** Execute a REPL expression. */
    exec(code) {
        if (!this._readline) return;
        const len = this._readline.writeInputBuf(code);
        this._exports.cp_exec(len);
        this._readline._waitingForResult = true;
    }

    /** Send Ctrl-C: interrupt running code + kill background contexts. */
    ctrlC() {
        this._exports.cp_ctrl_c();
        // Kill all non-zero running/sleeping contexts
        for (let i = 1; i < this._ctxMax; i++) {
            const m = this._readContextMeta(i);
            if (m && m.status >= 2 && m.status <= 5) {
                this._exports.cp_context_destroy(i);
            }
        }
        if (this._readline) this._readline.handleInterrupt();
    }

    /** Send Ctrl-D: soft reboot. */
    ctrlD() {
        this._exports.cp_ctrl_d();
        if (this._readline) {
            this._readline.termWrite('\r\n');
            this._readline._waitingForResult = true;
        }
    }

    /** Type a single character. */
    keypress(key) {
        if (this._readline) this._readline.handleKey(key, false, false);
    }

    /** Pause the frame loop (e.g. tab hidden). */
    pause() {
        if (this._raf) {
            env.cancelFrame(this._raf);
            this._raf = null;
        }
    }

    /** Resume the frame loop (e.g. tab visible). */
    resume() {
        if (!this._raf) {
            this._raf = env.requestFrame(() => this._loop());
        }
    }

    /** Clean up all resources. */
    destroy() {
        this.pause();
        if (env.hasDOM) {
            if (this._visibilityHandler) {
                document.removeEventListener('visibilitychange', this._visibilityHandler);
            }
            if (this._keyHandler) {
                document.removeEventListener('keydown', this._keyHandler);
            }
        }
        if (this._stdinHandler) {
            process.stdin.removeListener('data', this._stdinHandler);
            if (process.stdin.isTTY) process.stdin.setRawMode(false);
            this._stdinHandler = null;
        }
    }

    get state() {
        if (!this._exports) return 'loading';
        // Read context 0 status
        const m = this._readContextMeta(0);
        if (!m || m.status <= 1 || m.status === 6) return 'repl';
        return 'running';
    }

    get frameCount() { return this._frameCount; }
    get canvas() { return this._display?._canvas; }
    get displayWidth() { return this._display?.width || 0; }
    get displayHeight() { return this._display?.height || 0; }

    // ── Internal ──

    async _init(options) {
        // Default stdout/stderr for Node.js: write to process streams
        if (env.isNode) {
            this._onStdout = options.onStdout || ((text) => process.stdout.write(text));
            this._onStderr = options.onStderr || ((text) => process.stderr.write(text));
        } else {
            this._onStdout = options.onStdout || null;
            this._onStderr = options.onStderr || null;
        }
        this._statusEl = options.statusEl || null;
        this._serialEl = options.serialEl || null;
        this._onCodeDone = options.onCodeDone || null;

        if (this._statusEl) this._statusEl.textContent = 'Loading...';

        // Semihosting (shared-memory FFI: event ring + state export)
        this._sh = new Semihosting();

        // IndexedDB persistence
        const idb = options.persist ? new IdbBackend() : null;

        // WASI runtime
        this._wasi = new WasiMemfs({
            args: ['circuitpython'],
            idb,
            onStdout: (text) => this._handleStdout(text),
            onStderr: (text) => {
                if (this._onStderr) this._onStderr(text);
                else console.log('[stderr]', text);
            },
            onHardwareWrite: (path, data) => {
                if (!path.startsWith('/hal/serial')) {
                    console.log('[hal write]', path, data.length, 'bytes');
                }
            },
        });

        // Restore persisted files
        if (idb) {
            if (this._statusEl) this._statusEl.textContent = 'Loading filesystem...';
            const restored = await idb.load(this._wasi);
            console.log(`[idb] restored ${restored} files`);
        }

        // Seed CIRCUITPY drive
        seedDrive(this._wasi, {
            codePy: options.codePy || '',
        });

        // Seed additional files
        if (options.files) {
            const enc = new TextEncoder();
            for (const [path, content] of Object.entries(options.files)) {
                if (!this._wasi.files.has(path)) {
                    this._wasi.writeFile(path,
                        typeof content === 'string' ? enc.encode(content) : content);
                }
            }
        }

        // Compile + instantiate WASM
        if (this._statusEl) this._statusEl.textContent = 'Compiling...';

        const bytes = await env.loadFile(options.wasmUrl);
        const module = await WebAssembly.compile(bytes);

        // Merge WASI + jsffi imports
        const imports = this._wasi.getImports();
        imports.jsffi = getJsffiImports();

        const instance = await WebAssembly.instantiate(module, imports);
        this._wasi.setInstance(instance);
        this._sh.setInstance(instance);
        jsffi_init(instance);
        this._exports = instance.exports;

        if (this._statusEl) this._statusEl.textContent = 'Initializing...';

        // Initialize the supervisor
        this._exports.cp_init();

        // Display (browser only)
        this._display = options.canvas
            ? new Display(options.canvas, this._exports)
            : null;

        if (this._statusEl && this._display) {
            this._statusEl.textContent =
                `Running (${this._display.width}x${this._display.height} display)`;
        }

        // Context info
        this._ctxMax = this._exports.cp_context_max();
        this._ctxMetaSize = this._exports.cp_context_meta_size();

        // Fwip
        this._fwip = new Fwip(this._wasi, {
            log: (msg) => { console.log(msg); this._handleStdout(msg + '\n'); },
            exports: this._exports,
        });

        // Readline
        this._readline = new Readline(this._exports, {
            fwip: this._fwip,
            ctxMax: this._ctxMax,
            readContextMeta: (id) => this._readContextMeta(id),
        });

        // DOM event listeners (browser only)
        if (env.hasDOM) {
            this._keyHandler = (e) => {
                if (this._readline.handleKey(e.key, e.ctrlKey, e.metaKey)) {
                    e.preventDefault();
                }
            };
            document.addEventListener('keydown', this._keyHandler);

            // Tab visibility — pause when hidden, resume when visible
            this._visibilityHandler = () => {
                if (document.hidden) this.pause();
                else this.resume();
            };
            document.addEventListener('visibilitychange', this._visibilityHandler);
        }

        // Node.js stdin (TTY raw mode for interactive REPL)
        if (env.isNode && process.stdin.isTTY) {
            process.stdin.setRawMode(true);
            process.stdin.resume();
            this._stdinHandler = (data) => {
                for (const byte of data) {
                    if (byte === 3) {         // Ctrl-C
                        this.ctrlC();
                    } else if (byte === 4) {  // Ctrl-D
                        if (!this._readline._line && !this._readline._lines) {
                            this.destroy();
                            process.exit(0);
                        }
                        this.ctrlD();
                    } else if (byte === 13) { // Enter
                        this._readline.handleKey('Enter', false, false);
                    } else if (byte === 127 || byte === 8) { // Backspace
                        this._readline.handleKey('Backspace', false, false);
                    } else if (byte === 9) {  // Tab
                        this._readline.handleKey('Tab', false, false);
                    } else if (byte === 27) { // Escape sequence start
                        // Arrow keys come as \x1b[A/B/C/D — handled below
                    } else if (byte >= 32 && byte < 127) {
                        this._readline.handleKey(String.fromCharCode(byte), false, false);
                    }
                }
                // Handle escape sequences (arrow keys)
                if (data.length === 3 && data[0] === 27 && data[1] === 91) {
                    const arrows = { 65: 'ArrowUp', 66: 'ArrowDown', 67: 'ArrowRight', 68: 'ArrowLeft' };
                    const key = arrows[data[2]];
                    if (key) this._readline.handleKey(key, false, false);
                }
            };
            process.stdin.on('data', this._stdinHandler);
        }

        // Start frame loop
        this._raf = env.requestFrame(() => this._loop());
    }

    _handleStdout(text) {
        if (this._onStdout) {
            // Node terminals handle ANSI natively; only strip for DOM
            this._onStdout(this._serialEl ? text.replace(ANSI_RE, '') : text);
        }
        if (this._serialEl) {
            const clean = text.replace(ANSI_RE, '');
            this._serialText += clean;
            this._serialEl.textContent = this._serialText;
            this._serialEl.scrollTop = this._serialEl.scrollHeight;
        }
    }

    _readContextMeta(id) {
        if (!this._exports?.cp_context_meta_addr) return null;
        const addr = this._exports.cp_context_meta_addr(id);
        if (!addr) return null;
        const view = new DataView(this._exports.memory.buffer, addr, this._ctxMetaSize);
        return {
            status: view.getUint8(0),
            priority: view.getUint8(1),
            pystackCurOff: view.getUint32(4, true),
            yieldStateOff: view.getUint32(8, true),
        };
    }

    _loop() {
        const supState = this._exports.cp_step(performance.now() | 0);

        if (this._display) {
            this._display.paint();
            this._display.drawCursor();
        }

        this._frameCount++;

        // When expression finishes → show prompt
        if (supState === SUP_REPL && this._readline.waitingForResult) {
            this._readline.onResult();

            // Fire onCodeDone once when code.py finishes (transitions to REPL)
            if (this._onCodeDone && !this._codeDoneFired) {
                this._codeDoneFired = true;
                this._onCodeDone();
            }
        }

        // Status bar update (every ~1 second)
        if (this._statusEl && this._frameCount % 60 === 0) {
            const state = this._sh.readState();
            let activeCtxs = 0;
            for (let i = 0; i < this._ctxMax; i++) {
                const m = this._readContextMeta(i);
                if (m && m.status > 0) activeCtxs++;
            }
            if (state) {
                const sup = SUP_STATES[state.supState] || '?';
                const yr = YIELD_REASONS[state.yieldReason] || '?';
                const ctx0 = this._readContextMeta(0);
                const ctxSt = ctx0 ? CTX_STATUSES[ctx0.status] || '?' : '?';
                this._statusEl.textContent =
                    `${sup} | ctx0:${ctxSt} | ctxs:${activeCtxs} | yield:${yr} | pystack:${state.vmDepth}B`;
            } else {
                this._statusEl.textContent = `Running (frame ${this._frameCount})`;
            }
        }

        this._raf = env.requestFrame(() => this._loop());
    }
}
