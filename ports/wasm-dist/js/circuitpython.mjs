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
import { HardwareRouter, GpioModule, NeoPixelModule, AnalogModule, PwmModule, I2cModule, I2CDevice } from './hardware.mjs';
import { HardwareTarget, WebUSBTarget, WebSerialTarget, TeeTarget } from './targets.mjs';

const ANSI_RE = /\x1b\[[0-9;]*[A-Za-z]|\x1b\][^\x07]*\x07|\x1b\[[\?]?[0-9;]*[hlm]/g;

const SUP_REPL = 1;
const SUP_WAITING_FOR_KEY = 5;
const SUP_BOOT_RUNNING = 6;
const SUP_STATES = ['INIT', 'REPL', 'EXPR', 'CODE', 'DONE', 'WAIT_KEY', 'BOOT'];
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
        this._waitingForKey = false;
        this._hw = new HardwareRouter();
        this._ctxCallbacks = new Map();  // context id → onDone callback
        this._autoReloadTimer = null;
        this._autoReloadEnabled = false;
        this._prevSupState = 0;  // track transitions for "last edited" injection
        this._idb = null;        // saved for _printCodePyLastEdited
        this._target = null;     // external hardware target
        this._pollCounter = 0;   // input polling throttle
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
        this._waitingForKey = false;
        this._codeDoneFired = false;
        this._exports.cp_ctrl_d();
        if (this._readline) {
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
        if (this._autoReloadTimer) {
            clearTimeout(this._autoReloadTimer);
            this._autoReloadTimer = null;
        }
        if (this._target) {
            this._target.disconnect().catch(() => {});
            this._target = null;
        }
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
        if (this._waitingForKey) return 'waiting';
        const supState = this._exports.cp_get_state();
        if (supState === SUP_BOOT_RUNNING) return 'booting';
        // Read context 0 status
        const m = this._readContextMeta(0);
        if (!m || m.status <= 1 || m.status === 6) return 'repl';
        return 'running';
    }

    get frameCount() { return this._frameCount; }
    get canvas() { return this._display?._canvas; }
    get displayWidth() { return this._display?.width || 0; }
    get displayHeight() { return this._display?.height || 0; }

    /**
     * Register a hardware module.  Modules get preStep/postStep hooks
     * and receive routed /hal/ write/read callbacks.
     * @param {HardwareModule} mod
     */
    registerHardware(mod) { this._hw.register(mod); }

    /**
     * Get a registered hardware module by name.
     * @param {string} name — e.g., 'gpio', 'neopixel'
     * @returns {HardwareModule|null}
     */
    hardware(name) { return this._hw.get(name); }

    // ── Multi-context API ──

    /**
     * Run Python code in a background context.
     * The code runs concurrently with the REPL / code.py, scheduled by
     * priority (lower number = higher priority).
     *
     * @param {string} code — Python source code
     * @param {object} [options]
     * @param {number} [options.priority=200] — scheduling priority (0=highest)
     * @param {function} [options.onDone] — called when context finishes (id, error?)
     * @returns {number} context id (1–7), or -1 (no slots), or -2 (compile error)
     */
    runCode(code, options = {}) {
        const { priority = 200, onDone = null } = options;
        const len = this._readline.writeInputBuf(code);
        const id = this._exports.cp_context_exec(len, priority);
        if (id >= 0 && onDone) {
            this._ctxCallbacks.set(id, onDone);
        }
        return id;
    }

    /**
     * Run a .py file in a background context.
     * The file must exist in the CIRCUITPY drive (MEMFS).
     *
     * @param {string} path — file path (e.g., '/sensors.py')
     * @param {object} [options]
     * @param {number} [options.priority=200] — scheduling priority
     * @param {function} [options.onDone] — called when context finishes
     * @returns {number} context id, -1 (no slots), or -2 (compile error)
     */
    runFile(path, options = {}) {
        const { priority = 200, onDone = null } = options;
        const len = this._readline.writeInputBuf(path);
        const id = this._exports.cp_context_exec_file(len, priority);
        if (id >= 0 && onDone) {
            this._ctxCallbacks.set(id, onDone);
        }
        return id;
    }

    /**
     * List all active contexts with their status.
     * @returns {Array<{id, status, statusName, priority}>}
     */
    listContexts() {
        const result = [];
        for (let i = 0; i < this._ctxMax; i++) {
            const m = this._readContextMeta(i);
            if (m && m.status > 0) {
                result.push({
                    id: i,
                    status: m.status,
                    statusName: CTX_STATUSES[m.status] || '?',
                    priority: m.priority,
                });
            }
        }
        return result;
    }

    /**
     * Kill a background context.  Cannot kill context 0 (main).
     * @param {number} id — context id (1–7)
     * @returns {boolean} true if killed
     */
    killContext(id) {
        if (id <= 0 || id >= this._ctxMax) return false;
        const m = this._readContextMeta(id);
        if (!m || m.status === 0) return false;
        // Ensure we're not destroying the active context
        const active = this._exports.cp_context_active();
        if (active === id) {
            this._exports.cp_context_save(id);
            this._exports.cp_context_restore(0);
        }
        this._exports.cp_context_destroy(id);
        this._ctxCallbacks.delete(id);
        return true;
    }

    /** Number of active (non-free) contexts. */
    get activeContextCount() {
        let count = 0;
        for (let i = 0; i < this._ctxMax; i++) {
            const m = this._readContextMeta(i);
            if (m && m.status > 0) count++;
        }
        return count;
    }

    // ── External hardware targets ──

    /**
     * Connect an external hardware target.
     * Target receives /hal/ state diffs each frame and forwards to
     * real hardware via WebUSB, WebSerial, or both (Tee).
     *
     * @param {HardwareTarget} target — connected target
     */
    connectTarget(target) {
        this._target = target;
        // Wire input data back to MEMFS
        target.onInput((type, pin, value) => {
            if (type === 'gpio') {
                const gpio = this.hardware('gpio');
                if (gpio) gpio.setInputValue(this._wasi, pin, value);
            }
        });
    }

    /** Disconnect the current hardware target. */
    async disconnectTarget() {
        if (this._target) {
            await this._target.disconnect();
            this._target = null;
        }
    }

    /** @returns {HardwareTarget|null} The currently connected target. */
    get target() { return this._target; }

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
        this._idb = idb;

        // Register default hardware modules
        this._hw.register(new GpioModule());
        this._hw.register(new NeoPixelModule());
        this._hw.register(new AnalogModule());
        this._hw.register(new PwmModule());
        this._hw.register(new I2cModule());

        // WASI runtime — route /hal/ callbacks through hardware router
        this._wasi = new WasiMemfs({
            args: ['circuitpython'],
            idb,
            onStdout: (text) => this._handleStdout(text),
            onStderr: (text) => {
                if (this._onStderr) this._onStderr(text);
                else console.log('[stderr]', text);
            },
            onHardwareWrite: (path, data) => {
                this._hw.onWrite(path, data);
            },
            onHardwareRead: (path, offset) => {
                return this._hw.onRead(path, offset);
            },
            onFileChanged: (path) => {
                // Auto-reload when .py files under /CIRCUITPY/ change
                if (this._autoReloadEnabled &&
                    path.startsWith('/CIRCUITPY/') && path.endsWith('.py')) {
                    this._scheduleAutoReload();
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
        // Pass codePy as-is — seedDrive uses DEFAULT_CODE_PY when undefined/null
        seedDrive(this._wasi, {
            codePy: options.codePy,
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

        // Configure debug output before cp_init so init messages respect it.
        // Explicit option takes priority; otherwise check settings.toml.
        if (options.debug !== undefined) {
            this._exports.cp_set_debug(options.debug ? 1 : 0);
        } else {
            const settings = this._wasi.readFile('/CIRCUITPY/settings.toml');
            if (settings) {
                const text = new TextDecoder().decode(settings);
                const m = text.match(/^\s*CIRCUITPY_DEBUG\s*=\s*(\S+)/m);
                if (m) {
                    const val = m[1].toLowerCase();
                    this._exports.cp_set_debug(
                        val === '0' || val === 'false' || val === 'no' ? 0 : 1
                    );
                }
            }
        }

        // Initialize the supervisor (prints banner, starts boot.py or code.py)
        this._exports.cp_init();
        this._prevSupState = this._exports.cp_get_state();

        // Inject "code.py last edited" if cp_init went directly to code.py
        // (no boot.py).  When boot.py exists, injection happens in _loop
        // after boot.py finishes and code.py starts.
        if (this._prevSupState === 3 /* SUP_CODE_RUNNING */) {
            this._printCodePyLastEdited(idb);
        }

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
                // Intercept any key during WAITING_FOR_KEY → enter REPL
                if (this._waitingForKey) {
                    this._enterRepl();
                    e.preventDefault();
                    return;
                }
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
                // Intercept any key during WAITING_FOR_KEY → enter REPL
                if (this._waitingForKey) {
                    this._enterRepl();
                    return;
                }
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

        // Enable auto-reload now that init is complete
        this._autoReloadEnabled = true;

        // Start frame loop
        this._raf = env.requestFrame(() => this._loop());
    }

    /** Print "code.py last edited: ..." line via cp_print. */
    _printCodePyLastEdited(idb) {
        const codePyPath = '/CIRCUITPY/code.py';
        let line;
        if (idb && idb.mtimes.has(codePyPath)) {
            const mtime = idb.mtimes.get(codePyPath);
            const dt = new Date(mtime);
            const stamp = dt.toLocaleString(undefined, {
                dateStyle: 'medium', timeStyle: 'short',
            });
            line = `code.py last edited: ${stamp}\r\n`;
        } else if (this._wasi.files.has(codePyPath)) {
            line = `code.py last edited: Never\r\n`;
        } else {
            return;  // no code.py at all
        }
        // Write via cp_print so it appears on displayio + serial.
        // Readline isn't created yet, so use the export directly.
        const enc = new TextEncoder();
        const bytes = enc.encode(line);
        const addr = this._exports.cp_input_buf_addr();
        new Uint8Array(this._exports.memory.buffer, addr, bytes.length).set(bytes);
        this._exports.cp_print(bytes.length);
    }

    /** Schedule an auto-reload after a debounce period (500ms). */
    _scheduleAutoReload() {
        if (this._autoReloadTimer) clearTimeout(this._autoReloadTimer);
        this._autoReloadTimer = setTimeout(() => {
            this._autoReloadTimer = null;
            if (!this._exports) return;
            this._waitingForKey = false;
            this._codeDoneFired = false;
            this._exports.cp_auto_reload();
            if (this._readline) {
                this._readline._waitingForResult = true;
            }
        }, 500);
    }

    /** Transition from WAITING_FOR_KEY to REPL. */
    _enterRepl() {
        this._waitingForKey = false;
        this._exports.cp_press_any_key();
        // Show the REPL prompt
        this._readline._waitingForResult = false;
        this._readline.showPrompt();
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
        const nowMs = performance.now() | 0;

        // Pre-step: let hardware modules inject fresh data
        this._hw.preStep(this._wasi, nowMs);

        const supState = this._exports.cp_step(nowMs);

        // Post-step: let hardware modules read output state
        this._hw.postStep(this._wasi, nowMs);

        // Forward state to external hardware target (if connected)
        if (this._target && this._target.connected) {
            this._target.applyState(this._wasi, nowMs);
            // Poll inputs at reduced rate (~3 times/second)
            if (++this._pollCounter >= 20) {
                this._pollCounter = 0;
                this._target.pollInputs();
            }
        }

        if (this._display) {
            this._display.paint();
            this._display.drawCursor();
        }

        this._frameCount++;

        // Detect boot.py → code.py transition: inject "last edited" timestamp
        if (this._prevSupState === SUP_BOOT_RUNNING && supState !== SUP_BOOT_RUNNING) {
            if (supState === 3 /* SUP_CODE_RUNNING */) {
                this._printCodePyLastEdited(this._idb);
            }
        }
        this._prevSupState = supState;

        // When code.py finishes → enter WAITING_FOR_KEY state
        if (supState === SUP_WAITING_FOR_KEY && !this._waitingForKey) {
            this._waitingForKey = true;

            // Fire onCodeDone callback (e.g. for --exit mode)
            if (this._onCodeDone && !this._codeDoneFired) {
                this._codeDoneFired = true;
                this._onCodeDone();
            }
        }

        // When expression finishes → show prompt
        if (supState === SUP_REPL && this._readline.waitingForResult) {
            this._readline.onResult();
        }

        // Background context lifecycle: detect done contexts, fire callbacks, auto-cleanup.
        // cp_context_destroy requires the target not be the active context,
        // so save/restore around it if needed.
        for (let i = 1; i < this._ctxMax; i++) {
            const m = this._readContextMeta(i);
            if (!m || m.status !== 6) continue;  // only DONE contexts

            const cb = this._ctxCallbacks.get(i);
            if (cb) {
                this._ctxCallbacks.delete(i);
                cb(i, null);
            }

            // Ensure we're not destroying the active context
            const active = this._exports.cp_context_active();
            if (active === i) {
                this._exports.cp_context_save(i);
                this._exports.cp_context_restore(0);
            }
            this._exports.cp_context_destroy(i);
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

// Re-export hardware module classes for external use
export { HardwareModule, HardwareRouter, GpioModule, NeoPixelModule, AnalogModule, PwmModule, I2cModule, I2CDevice } from './hardware.mjs';
export { HardwareTarget, WebUSBTarget, WebSerialTarget, TeeTarget } from './targets.mjs';
