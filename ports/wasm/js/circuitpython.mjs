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
import { VMContext, DisplayContext, HardwareContext, IOContext } from './context-managers.mjs';

const ANSI_RE = /\x1b\[[0-9;]*[A-Za-z]|\x1b\][^\x07]*\x07|\x1b\[[\?]?[0-9;]*[hlm]/g;

// Context-status display names (matches CTX_* in supervisor/context.h)
const YIELD_REASONS = ['budget', 'sleep', 'show', 'io_wait', 'stdin'];
const CTX_STATUSES = ['FREE', 'IDLE', 'RUNNABLE', 'RUNNING', 'YIELDED', 'SLEEPING', 'DONE'];

// cp_exec() kind constants (must match supervisor.c)
const CP_EXEC_STRING = 0;
const CP_EXEC_FILE   = 1;

// cp_run() ctx constants (background contexts, must match supervisor.c)
const CP_SRC_EXPR = 0;
const CP_SRC_FILE = 1;
const CP_CTX_NEW  = -2;

// Auto-reload message — printed between boot.py and code.py by runBoardLifecycle
const AUTO_RELOAD_MSG =
    'Auto-reload is on. Simply save files over USB to run them or enter REPL to disable.\r\n';

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
        this._prevState = 0;     // for cp_state() transition detection (legacy)
        this._ctx0IsCode = false; // true when ctx0 is running a file (vs expr)
        this._idb = null;        // saved for _printCodePyLastEdited

        // Context managers — initialized after _init sets up exports
        this._managers = null;
        this._target = null;     // external hardware target
        this._pollCounter = 0;   // input polling throttle
    }

    // ── Public API ──

    /** Execute a Python string on ctx0. Source-agnostic. */
    exec(code) {
        if (!this._readline) return;
        // Auto-enter REPL if waiting for keypress
        if (this._waitingForKey) this._enterRepl();
        const len = this._readline.writeInputBuf(code);
        this._exports.cp_exec(CP_EXEC_STRING, len);
        this._ctx0IsCode = false;
        this._readline._waitingForResult = true;
        this._kick();
    }

    /** Execute a .py file from MEMFS on ctx0.
     *  @param {string} path — e.g. '/code.py'
     *  @returns {number} 0=started, -1=busy, -2=compile error */
    execFile(path) {
        const len = this._writeInputBuf(path);
        const r = this._exports.cp_exec(CP_EXEC_FILE, len);
        if (r === 0) {
            this._ctx0IsCode = true;
            this._codeDoneFired = false;
            if (this._readline) this._readline._waitingForResult = true;
        }
        this._kick();
        return r;
    }

    /** Wake a suspended context. Call after a Promise resolves (timer,
     *  I/O, user event) to resume execution.
     *  @param {number} [ctxId=0] — context to wake (-1 = all) */
    wake(ctxId = 0) {
        this._exports.cp_wake(ctxId);
        this._kick();
    }

    /** Stop execution + reset to READY state.
     *  Interrupts running code, kills background contexts, resets
     *  hardware (pins, buses, display). After this, state === 'ready'. */
    stop() {
        this.ctrlC();
        this._exports.cp_cleanup?.();
        // Reset all context managers (each resets its own domain)
        if (this._managers) {
            for (const mgr of this._managers) {
                mgr.reset();
            }
        }
        this._waitingForKey = false;
        this._codeDoneFired = false;
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
        this._kick();
    }

    /** Send Ctrl-D: soft reboot. */
    ctrlD() {
        this._waitingForKey = false;
        this._codeDoneFired = false;
        // C prints "soft reboot" and stops ctx0; JS re-invokes the lifecycle
        // to run boot.py / code.py again (no implicit restart in C anymore).
        this._exports.cp_ctrl_d();
        if (this._readline) {
            this._readline._waitingForResult = true;
        }
        this.runBoardLifecycle();
        this._kick();
    }

    /** Type a single character. */
    keypress(key) {
        if (this._readline) this._readline.handleKey(key, false, false);
        this._kick();
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
        this._destroyed = true;
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
        const s = this._exports.cp_state();
        if (s === 1) return 'executing';
        if (s === 2) return 'suspended';
        return 'ready';  // 0
    }

    get frameCount() { return this._frameCount; }
    get canvas() { return this._display?._canvas; }
    get displayWidth() { return this._display?.width || 0; }
    get displayHeight() { return this._display?.height || 0; }

    /** Access context managers for fine-grained control. */
    get displayContext() { return this._displayCtx; }

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

    // ── Board lifecycle orchestration ──

    /**
     * Run the traditional CircuitPython board lifecycle:
     *   banner → boot.py → auto-reload msg → code.py last-edited →
     *   code.py header → code.py → (wait-for-key) → REPL
     *
     * All stages are optional; the default matches real-board behavior.
     * Safe to call multiple times (soft-reboot path re-invokes it).
     *
     * The returned promise resolves once code.py has been dispatched
     * (not when it finishes — ongoing execution is handled by _loop).
     *
     * @param {object} [options]
     * @param {boolean} [options.printBanner=true]
     * @param {boolean} [options.bootPy=true]   — attempt /boot.py if present
     * @param {boolean} [options.autoReloadMsg=true]
     * @param {boolean} [options.codePy=true]   — attempt /code.py if present
     * @returns {Promise<void>}
     */
    async runBoardLifecycle(options = {}) {
        const {
            printBanner = true,
            bootPy = true,
            autoReloadMsg = true,
            codePy = true,
        } = options;

        if (printBanner) this._exports.cp_banner();

        if (bootPy) {
            if (this._runMainFile('/boot.py') === 0) {
                await this._awaitCtx0Idle();
            }
            // Non-zero return = no /boot.py (compile failed) — skip silently.
        }

        if (autoReloadMsg) this._handleStdout(AUTO_RELOAD_MSG);

        if (codePy) {
            this._printCodePyLastEdited(this._idb);
            if (this._runMainFile('/code.py') === 0) {
                this._handleStdout('code.py output:\r\n');
                await this._awaitCtx0Idle();
                // code.py finished — fire wait-for-key UX.  runBoardLifecycle
                // owns this because it knows which run was the user-visible
                // "code.py" (vs. the preceding boot.py, which doesn't trigger
                // wait-for-key even though it's also an SRC_FILE run on ctx0).
                this._waitingForKey = true;
                this._handleStdout('\r\nCode done running.\r\n');
                this._handleStdout('\r\nPress any key to enter the REPL. Use CTRL-D to reload.\r\n');
                if (this._onCodeDone && !this._codeDoneFired) {
                    this._codeDoneFired = true;
                    this._onCodeDone();
                }
            }
        }
    }

    /** Write a string to the shared input buffer. Returns bytes written. */
    _writeInputBuf(text) {
        const enc = new TextEncoder();
        const bytes = enc.encode(text);
        const addr = this._exports.cp_input_buf_addr();
        const cap = this._exports.cp_input_buf_size() - 1;
        const len = Math.min(bytes.length, cap);
        new Uint8Array(this._exports.memory.buffer, addr, len)
            .set(bytes.subarray(0, len));
        return len;
    }

    /** Start a .py file on ctx0 via cp_exec(CP_EXEC_FILE). */
    _runMainFile(path) {
        const len = this._writeInputBuf(path);
        const r = this._exports.cp_exec(CP_EXEC_FILE, len);
        this._kick();
        return r;
    }

    /** Resolve once ctx0 is idle/done/free. */
    _awaitCtx0Idle() {
        return new Promise((resolve) => {
            const check = () => {
                if (!this._exports) { resolve(); return; }
                const m = this._readContextMeta(0);
                // 0=FREE 1=IDLE 6=DONE — anything else means still running
                if (!m || m.status === 0 || m.status === 1 || m.status === 6) {
                    resolve();
                    return;
                }
                setTimeout(check, 16);
            };
            check();
        });
    }

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
        const r = this._exports.cp_run(CP_SRC_EXPR, len, CP_CTX_NEW, priority);
        // Normalize cp_run codes to legacy runCode contract: -1=no slots, -2=compile error
        const id = r >= 0 ? r : (r === -3 ? -1 : -2);
        if (id >= 0 && onDone) {
            this._ctxCallbacks.set(id, onDone);
        }
        this._kick();
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
        const r = this._exports.cp_run(CP_SRC_FILE, len, CP_CTX_NEW, priority);
        const id = r >= 0 ? r : (r === -3 ? -1 : -2);
        if (id >= 0 && onDone) {
            this._ctxCallbacks.set(id, onDone);
        }
        this._kick();
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

        // Initialize the supervisor (core init only — no auto-lifecycle).
        // JS orchestrates boot.py → code.py → REPL via runBoardLifecycle().
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
            onExec: (len) => {
                // Route readline exec through the CircuitPython class
                // so state tracking (_ctx0IsCode, _kick) stays in sync.
                const r = this._exports.cp_exec(CP_EXEC_STRING, len);
                if (r === 0) {
                    this._ctx0IsCode = false;
                    this._kick();
                }
                return r;
            },
        });

        // DOM event listeners (browser only)
        if (env.hasDOM) {
            this._keyHandler = (e) => {
                // Don't capture keys when the user is typing in an input,
                // textarea, or contenteditable element (e.g., the code editor).
                const tag = e.target?.tagName;
                if (tag === 'INPUT' || tag === 'TEXTAREA' || e.target?.isContentEditable) {
                    return;
                }

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

            // Tab visibility — notify managers + pause/resume loop
            this._visibilityHandler = () => {
                if (document.hidden) {
                    if (this._managers) {
                        for (const mgr of this._managers) mgr.onHidden();
                    }
                    this.pause();
                } else {
                    if (this._managers) {
                        for (const mgr of this._managers) mgr.onVisible();
                    }
                    this.resume();
                }
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

        // Enable auto-reload — only when using runBoardLifecycle.
        // When autoLifecycle=false, JS manages execution directly.
        this._autoReloadEnabled = (options.autoLifecycle !== false);

        // Initialize context managers
        this._vmCtx = new VMContext(this);
        this._displayCtx = new DisplayContext(this);
        this._hwCtx = new HardwareContext(this);
        this._ioCtx = new IOContext(this);
        this._managers = [this._vmCtx, this._displayCtx, this._hwCtx, this._ioCtx];

        // Start frame loop
        this._raf = env.requestFrame(() => this._loop());

        // Kick off the traditional board lifecycle (banner → boot → code → REPL).
        // Fire-and-forget: _loop advances ctx0 each frame; runBoardLifecycle
        // awaits ctx0 idle between stages.  Callers can pass autoLifecycle:false
        // to skip and orchestrate manually (e.g., headless test harnesses).
        if (options.autoLifecycle !== false) {
            this.runBoardLifecycle();
        }
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
            // Clean up Layer 3 (no "soft reboot" message — auto-reload
            // is silent), then re-run the lifecycle.
            this._exports.cp_cleanup();
            if (this._readline) {
                this._readline._waitingForResult = true;
            }
            this.runBoardLifecycle();
            this._kick();
        }, 500);
    }

    /** Transition from the "wait for key" UX to REPL.
     *  Wait-for-key is now JS-owned: no C call, we just clear the flag
     *  and show the prompt.  One "\r\n" separates the banner from the prompt. */
    _enterRepl() {
        this._waitingForKey = false;
        // Reset display/pins/buses so REPL starts on a clean terminal
        this._exports.cp_cleanup?.();
        this._handleStdout('\r\n');
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

        // Step each context manager that has work
        for (const mgr of this._managers) {
            if (mgr.needsWork) {
                mgr.step(nowMs);
            }
        }

        this._frameCount++;

        // Status bar update (every ~1 second)
        if (this._statusEl && this._frameCount % 60 === 0) {
            const STATE_NAMES = ['READY', 'EXEC', 'SUSPEND'];
            const st = this._exports?.cp_state?.() ?? 0;
            this._statusEl.textContent = `${STATE_NAMES[st] || '?'} | frame:${this._frameCount}`;
        }

        this._scheduleNext(nowMs);
    }

    /**
     * Schedule the next _loop() call based on VM state.
     *
     * We ask the VM how many ms until it needs attention:
     *   0          → runnable now: requestAnimationFrame (full rate)
     *   1..N ms    → sleeping: still tick at full rate for HAL/display
     *               (cp_step is cheap during sleep — it skips the VM
     *               phase and only runs HAL + background callbacks)
     *   0xFFFFFFFF → truly idle (no contexts): don't schedule.
     *               _kick() restarts on external event (keypress,
     *               file write, cp_run call).  This is where we save
     *               battery on mobile — no work, no ticks.
     */
    _scheduleNext(nowMs) {
        // Check if any manager needs work
        const anyWork = this._managers?.some(m => m.needsWork) ?? false;
        if (anyWork) {
            // At least one manager has work — keep ticking
            this._raf = env.requestFrame(() => this._loop());
        } else {
            // All idle — stop loop; _kick() restarts on external event
            this._raf = null;
        }
    }

    /**
     * Kick the frame loop awake after an external event.
     * Called by ctrlC, ctrlD, keypress, cp_run, auto-reload, etc.
     * If the loop is already scheduled, this is a no-op.
     */
    _kick() {
        if (!this._raf && !this._destroyed) {
            this._raf = env.requestFrame(() => this._loop());
        }
    }
}

// Re-export hardware module classes for external use
export { HardwareModule, HardwareRouter, GpioModule, NeoPixelModule, AnalogModule, PwmModule, I2cModule, I2CDevice } from './hardware.mjs';
export { HardwareTarget, WebUSBTarget, WebSerialTarget, TeeTarget } from './targets.mjs';
