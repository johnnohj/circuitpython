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

import { WasiMemfs, IdbBackend, seedDrive } from './wasi.js';
import { Wasi2Memfs } from './wasi2.js';
import { getJsffiImports, jsffi_init } from './ffi.js';
import { env } from './env.mjs';
import { Fwip } from './fwip.js';
import { Display } from './display.mjs';
import { HardwareState, I2CDevice } from './hardware.mjs';
import { HardwareTarget, WebUSBTarget, WebSerialTarget, TeeTarget } from './targets.mjs';

// ── Frame result unpacking ──
// Packed as (port | sup<<8 | vm<<16) by port_frame() in port/main.c.
function unpackFrameResult(r) {
    return { port: r & 0xFF, sup: (r >> 8) & 0xFF, vm: (r >> 16) & 0xFF };
}

// Frame result constants — must match port/main.c
const WASM_SUP_CTX_DONE = 2;
const WASM_VM_SLEEPING  = 2;
const WASM_VM_COMPLETED = 3;

const ANSI_RE = /\x1b\[[0-9;]*[A-Za-z]|\x1b\][^\x07]*\x07|\x1b\[[\?]?[0-9;]*[hlm]/g;

// ── Context managers (inlined from context-managers.mjs) ──
// Output consumers for chassis_frame() results. They don't call C exports —
// they only read state and update JS-side resources.

class DisplayContext {
    constructor(board) {
        this._board = board;
        this._showCursor = false;
        this._cursorVisible = false;
    }
    startCursorBlink() {
        this._showCursor = true;
        this._cursorVisible = true;
    }
    stopCursorBlink() {
        this._showCursor = false;
        this._cursorVisible = false;
    }
    onVisible() {}
    onHidden() { this.stopCursorBlink(); }
    reset() { this.stopCursorBlink(); }
}

class HardwareContext {
    constructor(board) { this._board = board; }
    onVisible() {}
    onHidden() {}
    reset() {
        const board = this._board;
        if (board._hw?.reset) board._hw.reset(board._wasi);
    }
}

class IOContext {
    constructor(board) {
        this._board = board;
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
    onHidden() { this._pollInterval = 60; }
    onVisible() { this._pollInterval = 20; }
    reset() { this._pollCounter = 0; }
}

// cp_exec() kind constants (must match ffi_exports.c)
const CP_EXEC_STRING = 0;
const CP_EXEC_FILE   = 1;

// Auto-reload message — printed between boot.py and code.py by runBoardLifecycle
const AUTO_RELOAD_MSG =
    'Auto-reload is on. Simply save files over USB to run them or enter REPL to disable.\r\n';

// Default board definition — matches boards/wasm_browser/definition.json.
// Used when no boardDefinition option is provided to CircuitPython.create().
// Can be overridden at boot for board switching (Feather, CPX, etc.).
const DEFAULT_BOARD_DEFINITION = {
    boardName: 'wasm_browser',
    pins: [
        { name: 'D0',  id: 0 },  { name: 'D1',  id: 1 },
        { name: 'D2',  id: 2 },  { name: 'D3',  id: 3 },
        { name: 'D4',  id: 4 },  { name: 'D5',  id: 5 },
        { name: 'D6',  id: 6 },  { name: 'D7',  id: 7 },
        { name: 'D8',  id: 8 },  { name: 'D9',  id: 9 },
        { name: 'D10', id: 10 }, { name: 'D11', id: 11 },
        { name: 'D12', id: 12 },
        { name: 'D13', id: 13, aliases: ['LED'] },
        { name: 'A0',  id: 14 }, { name: 'A1',  id: 15 },
        { name: 'A2',  id: 16 }, { name: 'A3',  id: 17 },
        { name: 'A4',  id: 18, aliases: ['SDA'] },
        { name: 'A5',  id: 19, aliases: ['SCL'] },
        { name: 'NEOPIXEL', id: 20 },
        { name: 'BUTTON_A', id: 21, aliases: ['BUTTON'] },
        { name: 'BUTTON_B', id: 22 },
        { name: 'MOSI', id: 11 }, { name: 'MISO', id: 12 },
        { name: 'SCK',  id: 13 },
        { name: 'TX',   id: 1 },  { name: 'RX',   id: 0 },
    ],
};

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
        this._display = null;
        this._fwip = null;
        this._raf = null;
        this._frameCount = 0;
        this._statusEl = null;
        this._serialEl = null;
        this._serialText = '';
        this._onStdout = null;
        this._onStderr = null;
        this._visibilityHandler = null;
        this._keyHandler = null;
        this._stdinHandler = null;
        this._onCodeDone = null;
        this._codeDoneFired = false;
        this._waitingForKey = false;
        this._hw = new HardwareState();
        this._autoReloadTimer = null;
        this._autoReloadEnabled = false;
        this._ctx0IsCode = false; // true when ctx0 is running a file (vs expr)
        this._idb = null;        // saved for _printCodePyLastEdited

        // Context managers — initialized after _init sets up exports
        this._managers = null;
        this._target = null;     // external hardware target
        this._pollCounter = 0;   // input polling throttle

        // Serial ring buffer addresses (set by _initSerialRings after WASM init)
        this._serialTxBase = 0;
        this._inRepl = false;    // true after cp_start_repl(), for re-entry after exec
    }

    // ── Public API ──

    /** Execute a Python string on ctx0. Source-agnostic.
     *  This is the programmatic API — code is compiled and run directly,
     *  bypassing the REPL readline (no echo, no history). */
    exec(code) {
        if (!this._exports) return;
        if (this._waitingForKey) this._enterRepl();
        const len = this._writeInputBuf(code);
        this._exports.cp_exec(CP_EXEC_STRING, len);
        this._ctx0IsCode = false;
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
        }
        this._kick();
        return r;
    }

    /** Run a short frame so C sees dirty flags immediately.
     *  Then schedule a full frame for VM + display. */
    kick() {
        this._exports.chassis_frame((performance.now() * 1000) | 0, 2000);
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

    /** Send Ctrl-C: interrupt running code. */
    ctrlC() {
        this._exports.cp_ctrl_c();
        this._kick();
    }

    /** Send Ctrl-D: soft reboot. */
    ctrlD() {
        this._waitingForKey = false;
        this._codeDoneFired = false;
        this._inRepl = false;
        this._exports.cp_ctrl_d();
        this.runBoardLifecycle();
        this._kick();
    }

    /** Type a single character via the serial path. */
    keypress(key) {
        this._sendKeyToSerial(key, false, false);
    }

    /** Enter the C-side REPL.  The C readline handles prompt, editing,
     *  history, and tab completion — output appears via serial_tx. */
    startRepl() {
        if (!this._exports) return;
        this._enterRepl();
    }

    /** Set supervisor debug verbosity level (0=off, 1=on). */
    setDebug(level) {
        this._exports?.cp_set_debug(level);
    }

    /** Run a single frame synchronously (port → supervisor → VM).
     *  Useful when you need to flush state before the next rAF. */
    stepFrame() {
        if (!this._exports) return;
        this._exports.chassis_frame((performance.now() * 1000) | 0, 1000);
        this._flushSerial();
    }

    /** Read a file from the CIRCUITPY filesystem (MEMFS).
     *  @returns {Uint8Array|null} file contents, or null if not found */
    readFile(path) {
        return this._wasi?.readFile(path) ?? null;
    }

    /** Write a file to the CIRCUITPY filesystem (MEMFS).
     *  @param {string} path — e.g. '/CIRCUITPY/code.py'
     *  @param {Uint8Array|string} data — contents (strings are UTF-8 encoded) */
    writeFile(path, data) {
        if (!this._wasi) return;
        if (typeof data === 'string') data = new TextEncoder().encode(data);
        this._wasi.writeFile(path, data);
    }

    /** Write text through the C output path (displayio terminal + serial_tx).
     *  Use for programmatic messages, escape sequences, banners, etc.
     *  This is OUTPUT to the user — not input to the C readline. */
    print(text) {
        if (!this._exports) return;
        const len = this._writeInputBuf(text);
        this._exports.cp_print(len);
        this._flushSerial();
    }

    /** Push raw bytes into serial_rx as keyboard input to C.
     *  Goes through cp_serial_push → serial_rx ring → C readline/REPL. */
    typeInput(text) {
        if (!this._exports) return;
        for (let i = 0; i < text.length; i++) {
            this._exports.cp_serial_push(text.charCodeAt(i));
        }
        this._kick();
    }

    /** Clear the displayio terminal (ESC[2J). */
    clearTerminal() {
        this.print('\x1b[2J');
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

    /**
     * Debug/trace info from the VM — current line, source file, call depth.
     * Always available (cheap — reads linear memory). JS decides whether
     * to display it (opt-in via debug checkbox or API flag).
     */
    // traceInfo and drainTrace — not yet implemented for wasm-tmp.
    // Requires debug/trace region in port_mem (future work).
    get traceInfo() { return null; }
    drainTrace() { return []; }

    /** Access context managers for fine-grained control. */
    get displayContext() { return this._displayCtx; }

    /**
     * Register a hardware module.  Modules receive routed /hal/
     * onWrite/onRead callbacks from C, plus afterFrame for cleanup.
     * @param {HardwareModule} mod
     */
    /** @deprecated Use board.hardware('i2c').addDevice() instead. */
    registerHardware(mod) {}

    /**
     * Get a registered hardware module by name.
     * @param {string} name — e.g., 'gpio', 'neopixel'
     * @returns {HardwareModule|null}
     */
    hardware(name) { return this._hw.hardware(name); }

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

        if (printBanner) {
            this._exports.cp_banner();
            this._flushSerial();  // flush banner before JS-side messages
        }

        if (bootPy) {
            if (this._runMainFile('/CIRCUITPY/boot.py') === 0) {
                await this._awaitCtx0Idle();
                this._flushSerial();  // flush boot.py output
            }
        }

        if (autoReloadMsg) this._handleStdout(AUTO_RELOAD_MSG);

        if (codePy) {
            this._printCodePyLastEdited(this._idb);
            this._flushSerial();  // flush cp_print output
            if (this._runMainFile('/CIRCUITPY/code.py') === 0) {
                this._handleStdout('code.py output:\r\n');
                await this._awaitCtx0Idle();
                this._flushSerial();  // flush code.py output
                this._waitingForKey = true;
                this._handleStdout('\r\nCode done running.\r\n');
                this._handleStdout('\r\nPress any key to enter the REPL. Use CTRL-D to reload.\r\n');
                if (this._onCodeDone && !this._codeDoneFired) {
                    this._codeDoneFired = true;
                    this._onCodeDone();
                }
            } else {
                // No code.py or compile error — go straight to REPL
                this._enterRepl();
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
                // cp_state: 0=idle, 1=executing, 2=repl
                const state = this._exports.cp_state?.() ?? 0;
                if (state === 0) {
                    resolve();
                    return;
                }
                setTimeout(check, 16);
            };
            check();
        });
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
        this._onFrame = options.onFrame || null;

        if (this._statusEl) this._statusEl.textContent = 'Loading...';

        // IndexedDB persistence
        const idb = options.persist ? new IdbBackend() : null;
        this._idb = idb;

        // Compile WASM first to detect Preview 1 vs Preview 2
        if (this._statusEl) this._statusEl.textContent = 'Compiling...';
        const bytes = await env.loadFile(options.wasmUrl);
        const module = await WebAssembly.compile(bytes);
        const moduleImports = WebAssembly.Module.imports(module);
        const isP2 = moduleImports.some(i => i.module.startsWith('wasi:'));

        // WASI runtime — auto-detect Preview 1 or Preview 2
        const wasiOpts = {
            args: ['circuitpython'],
            idb,
            onStdout: (text) => this._handleStdout(text),
            onStderr: (text) => {
                if (this._onStderr) this._onStderr(text);
                else console.log('[stderr]', text);
            },
            onFileChanged: (path) => {
                if (this._autoReloadEnabled &&
                    path.startsWith('/CIRCUITPY/') && path.endsWith('.py')) {
                    this._scheduleAutoReload();
                }
            },
        };

        if (isP2) {
            this._wasi = new Wasi2Memfs(wasiOpts);
        } else {
            wasiOpts.onHardwareWrite = (path, data) => {
                this._hw.onWrite(path, data);
            };
            wasiOpts.onHardwareRead = (path, offset) => {
                return this._hw.onRead(path, offset);
            };
            this._wasi = new WasiMemfs(wasiOpts);
        }
        this._hw.setMemfs(this._wasi);

        // Restore persisted files
        if (idb) {
            if (this._statusEl) this._statusEl.textContent = 'Loading filesystem...';
            const restored = await idb.load(this._wasi);
            console.log(`[idb] restored ${restored} files`);
        }

        // Seed CIRCUITPY drive
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

        // Merge WASI + jsffi + port imports
        const imports = this._wasi.getImports();
        imports.jsffi = getJsffiImports();

        // Port imports — C calls these to request scheduling.
        // requestFrame(hint_ms): C tells JS when to call wasm_frame() next.
        //   0 = ASAP, 1..N = delay ms, 0xFFFFFFFF = idle (don't schedule)
        imports.port = {
            requestFrame: (hintMs) => {
                // C calls this at the end of each chassis_frame() to tell
                // JS when to call again.  JS always uses rAF for vsync —
                // the hint is informational (0=ASAP, 0xFFFFFFFF=idle).
                if (this._raf) {
                    env.cancelFrame(this._raf);
                    this._raf = null;
                }

                if (hintMs === 0xFFFFFFFF) {
                    // Fully idle — don't schedule. _kick() restarts on event.
                    return;
                }
                // Active — schedule via rAF (steady 60fps)
                this._raf = env.requestFrame(() => this._loop());
            },

            // ── Synchronous data callouts ──
            // Common-hal calls these via WASM imports. JS runs
            // synchronously, returns a value, C continues.

            getCpuTemperature: () => {
                // Simulated: 25°C ± 2°C noise
                return 25000 + ((Math.random() * 4000 - 2000) | 0);
            },

            getCpuVoltage: () => {
                return 3300;  // 3.3V
            },

            getMonotonicMs: () => {
                return performance.now() | 0;
            },

            // ── Pin listener registration ──
            // C calls these from common-hal digitalio construct/deinit.
            // JS attaches DOM event listeners to board image elements
            // so user interaction (click, mousedown/up) writes pin state
            // directly to port_mem via hardware.mjs.

            registerPinListener: (pin) => {
                if (typeof document === 'undefined') return;
                const el = document.querySelector(`[data-pin-id="${pin}"]`)
                        || document.querySelector(`[data-pin="${pin}"]`);
                if (!el) return;

                const gpio = this._hw;
                const MIN_HOLD_MS = 200;  // minimum press duration for VM to see it
                let holdTimer = null;

                const down = () => {
                    gpio.setGpioInput(pin, false);  // pressed = low
                    this._kick();
                    // Prevent release for MIN_HOLD_MS so the VM's polling
                    // loop has time to read the pressed state.
                    if (holdTimer) clearTimeout(holdTimer);
                    holdTimer = setTimeout(() => { holdTimer = null; }, MIN_HOLD_MS);
                };
                const up = () => {
                    if (holdTimer) {
                        // Release came too fast — defer it until hold expires
                        const remaining = MIN_HOLD_MS;
                        clearTimeout(holdTimer);
                        holdTimer = setTimeout(() => {
                            holdTimer = null;
                            gpio.setGpioInput(pin, true);
                            this._kick();
                        }, remaining);
                    } else {
                        gpio.setGpioInput(pin, true);   // released = high
                        this._kick();
                    }
                };

                el.addEventListener('mousedown', down);
                el.addEventListener('mouseup', up);
                // Touch support
                el.addEventListener('touchstart', (e) => { e.preventDefault(); down(); });
                el.addEventListener('touchend', (e) => { e.preventDefault(); up(); });

                // Stash handlers for cleanup on unregister
                if (!this._pinListeners) this._pinListeners = new Map();
                this._pinListeners.set(pin, { el, down, up });
            },

            unregisterPinListener: (pin) => {
                const entry = this._pinListeners?.get(pin);
                if (!entry) return;
                entry.el.removeEventListener('mousedown', entry.down);
                entry.el.removeEventListener('mouseup', entry.up);
                // Touch listeners use the same functions via wrapper,
                // but anonymous wrappers can't be removed — acceptable
                // for now since deinit typically precedes page unload.
                this._pinListeners.delete(pin);
            },
        };

        // ── ffi import module (port/ffi_imports.h) ──
        // New wasm-tmp C code uses "ffi" module for frame scheduling
        // and notifications.
        imports.ffi = {
            request_frame: (hintMs) => {
                imports.port.requestFrame(hintMs);
            },
            notify: (type, pin, arg, data) => {
                // C→JS notification (pin changed, serial data, etc.)
                // JS reads actual state from MEMFS — this is just a nudge.
            },
        };

        // ── memfs import module (port/memfs_imports.h) ──
        // C calls memfs.register(path_ptr, path_len, data_ptr, data_size)
        // to map port_mem regions as MEMFS files.  We store live
        // Uint8Array views into WASM linear memory so that readFile()
        // returns the same bytes that C sees (and writeFile modifies
        // them in place for C to read).
        const pendingRegistrations = [];
        imports.memfs = {
            register: (pathPtr, pathLen, dataPtr, dataSize) => {
                // Instance isn't set yet during _initialize, so defer.
                pendingRegistrations.push({ pathPtr, pathLen, dataPtr, dataSize });
            },
        };

        const instance = await WebAssembly.instantiate(module, imports);
        this._wasi.setInstance(instance);
        jsffi_init(instance);
        this._exports = instance.exports;
        this._hw.setExports(instance.exports);
        // (semihosting removed — hardware reads/writes use live views directly)
        this._initSerialRings();

        if (this._statusEl) this._statusEl.textContent = 'Initializing...';

        // Configure debug output before chassis_init so init messages respect it.
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
        this._exports.chassis_init();

        // Flush deferred memfs.register calls — create live Uint8Array
        // views into WASM linear memory for each registered region.
        // readFile('/hal/gpio') now returns the same bytes C sees.
        const mem = instance.exports.memory;
        for (const { pathPtr, pathLen, dataPtr, dataSize } of pendingRegistrations) {
            const path = new TextDecoder().decode(
                new Uint8Array(mem.buffer, pathPtr, pathLen));
            this._wasi.registerLiveView(path, mem, dataPtr, dataSize);
        }

        // Apply custom board definition if provided.  The C default
        // (compiled into board_pins.c) is used when no definition is given.
        if (options.boardDefinition) {
            this._applyBoardDefinition(options.boardDefinition);
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
        // Context info (single-context for now)

        // Fwip
        this._fwip = new Fwip(this._wasi, {
            log: (msg) => { console.log(msg); this._handleStdout(msg + '\n'); },
            exports: this._exports,
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
                // Route keystrokes through serial_rx → C-side readline.
                if (this._sendKeyToSerial(e.key, e.ctrlKey, e.metaKey)) {
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
                // Raw terminal bytes go straight through cp_serial_push.
                // The C-side readline handles editing, history, tab completion.
                for (const byte of data) {
                    if (byte === 3) {         // Ctrl-C
                        this.ctrlC();
                    } else if (byte === 4) {  // Ctrl-D
                        this.ctrlD();
                    } else {
                        this._exports.cp_serial_push(byte);
                    }
                }
                this._kick();
            };
            process.stdin.on('data', this._stdinHandler);
        }

        // Enable auto-reload — only when using runBoardLifecycle.
        // When autoLifecycle=false, JS manages execution directly.
        this._autoReloadEnabled = (options.autoLifecycle !== false);

        // Initialize output consumers (wasm_frame handles all C-side work)
        this._displayCtx = new DisplayContext(this);
        this._hwCtx = new HardwareContext(this);
        this._ioCtx = new IOContext(this);
        this._managers = [this._displayCtx, this._hwCtx, this._ioCtx];

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

    /** Store board definition for visual rendering (SVG, board-adapter).
     *  Board pins are defined in the static C table (board_pins.c) —
     *  pin_meta categories are set at init by hal_init_pin_categories(). */
    _applyBoardDefinition(def) {
        if (def) this._boardDefinition = def;
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
            this._inRepl = false;
            this.runBoardLifecycle();
            this._kick();
        }, 500);
    }

    /** Transition from the "wait for key" UX to REPL.
     *  Tells C to enter REPL mode — C-side readline handles the prompt,
     *  line editing, history, and tab completion.  Output comes through
     *  serial_tx, input goes through serial_rx (cp_serial_push). */
    _enterRepl() {
        this._waitingForKey = false;
        this._exports.cp_cleanup?.();
        this._exports.cp_start_repl();
        this._inRepl = true;
        this._kick();
    }

    _handleStdout(text) {
        if (this._onStdout) {
            this._onStdout(text.replace(ANSI_RE, ''));
        }
        if (this._serialEl) {
            const clean = text.replace(ANSI_RE, '');
            this._serialText += clean;
            this._serialEl.textContent = this._serialText;
            this._serialEl.scrollTop = this._serialEl.scrollHeight;
        }
    }

    // ── Serial ring buffer I/O ──
    // port_mem.serial_tx is the C→JS output ring.  After each chassis_frame(),
    // JS drains it and routes the text to _handleStdout (terminal display).
    // Keystrokes go the other direction via cp_serial_push() → serial_rx ring.

    _initSerialRings() {
        // Use the exported address — no hardcoded layout math.
        this._serialTxBase = this._exports.cp_serial_tx_addr();
    }

    /** Drain all pending bytes from the serial_tx ring.  Returns a string
     *  (empty if nothing to read).  Advances the read head. */
    _drainSerialTx() {
        if (!this._serialTxBase) return '';
        const buf = this._exports.memory.buffer;
        const view = new DataView(buf);
        const RING_HEADER = 8;
        const RING_DATA_SIZE = 4096 - RING_HEADER;

        const writeHead = view.getUint32(this._serialTxBase, true);
        let readHead = view.getUint32(this._serialTxBase + 4, true);
        if (readHead === writeHead) return '';

        const bytes = new Uint8Array(buf);
        const dataBase = this._serialTxBase + RING_HEADER;
        const chars = [];
        while (readHead !== writeHead) {
            chars.push(bytes[dataBase + readHead]);
            readHead = (readHead + 1) % RING_DATA_SIZE;
        }
        view.setUint32(this._serialTxBase + 4, readHead, true);
        return String.fromCharCode(...chars);
    }

    /** Drain serial_tx and display any output. */
    _flushSerial() {
        const text = this._drainSerialTx();
        if (text) this._handleStdout(text);
    }

    /** Translate a JS keydown event to serial bytes and push via cp_serial_push.
     *  Returns true if the key was handled. */
    _sendKeyToSerial(key, ctrlKey, metaKey) {
        // Meta-key combos (Cmd on Mac) should not be captured
        if (metaKey) return false;

        if (ctrlKey) {
            if (key === 'c' || key === 'C') { this.ctrlC(); return true; }
            if (key === 'd' || key === 'D') { this.ctrlD(); return true; }
            // Ctrl-A through Ctrl-Z → 0x01–0x1A
            if (key.length === 1) {
                const code = key.toLowerCase().charCodeAt(0);
                if (code >= 97 && code <= 122) {
                    this._exports.cp_serial_push(code - 96);
                    this._kick();
                    return true;
                }
            }
            return false;
        }

        const push = (c) => this._exports.cp_serial_push(c);
        switch (key) {
            case 'Enter':      push(0x0D); break;
            case 'Backspace':  push(0x7F); break;
            case 'Tab':        push(0x09); break;
            case 'ArrowUp':    push(27); push(91); push(65); break;
            case 'ArrowDown':  push(27); push(91); push(66); break;
            case 'ArrowRight': push(27); push(91); push(67); break;
            case 'ArrowLeft':  push(27); push(91); push(68); break;
            case 'Home':       push(27); push(91); push(72); break;
            case 'End':        push(27); push(91); push(70); break;
            case 'Delete':     push(27); push(91); push(51); push(126); break;
            case 'Escape':     return false;
            default:
                if (key.length === 1) {
                    push(key.charCodeAt(0));
                } else {
                    return false;
                }
        }
        this._kick();
        return true;
    }

    // ── Frame loop ──
    //
    // The frame loop follows the packet protocol (design/packet-protocol.md):
    //   1. Call chassis_frame() — C runs port + supervisor + VM
    //   2. Apply outbound packet — read what changed, update JS state
    //   3. C drives scheduling via ffi_request_frame(hint)
    //
    // The outbound packet is the packed frame result (port|sup<<8|vm<<16)
    // plus the state readable from port_mem (serial_tx, display frame_count,
    // hal change_count).  JS reads these and decides what to update.

    _loop() {
        const nowUs = (performance.now() * 1000) | 0;

        // Step 1: C does all port + supervisor + VM work
        const r = this._exports.chassis_frame(nowUs, 0);
        const { port, sup, vm } = unpackFrameResult(r);

        // Step 2: Apply outbound packet — update JS from what C changed
        this._applyFrameResult(port, sup, vm);

        this._frameCount++;
    }

    /** Apply the outbound packet: read what C changed, update JS state.
     *  This is the single place that interprets frame results. */
    _applyFrameResult(port, sup, vm) {
        // ── Serial output ──
        // Drain serial_tx ring. Cheap check (two u32 reads) when empty.
        this._flushSerial();

        // ── Display ──
        // paint() checks frame_count internally — no-op when unchanged.
        if (this._display) {
            this._display.paint();
            if (this._displayCtx?._showCursor && this._displayCtx?._cursorVisible) {
                this._display.drawCursor();
            }
        }

        // ── Hardware state ──
        // Notify frame listeners (SVG render, sensor panel, etc.)
        // only when the port layer reports changes.
        if (port >= 1 && this._onFrame) {
            this._onFrame();
        }

        // Post-frame hardware cleanup (release latched buttons, etc.)
        this._hw.afterFrame(this._wasi);

        // ── IO target polling ──
        if (this._ioCtx?.needsWork) {
            this._ioCtx.step(performance.now());
        }

        // ── Code completion ──
        if (sup === WASM_SUP_CTX_DONE) {
            this._exports.cp_display_refresh?.();

            // Re-enter REPL after programmatic exec() completes
            if (this._inRepl) {
                this._exports.cp_start_repl();
            }

            if (this._onCodeDone && !this._codeDoneFired && this._ctx0IsCode) {
                this._codeDoneFired = true;
                this._onCodeDone();
            }
        }

        // ── Status bar ── (every ~1s)
        if (this._statusEl && this._frameCount % 60 === 0) {
            const PHASE = ['IDLE', 'CODE', 'REPL'];
            const st = this._exports?.cp_state?.() ?? 0;
            this._statusEl.textContent = `${PHASE[st] || '?'} | frame ${this._frameCount}`;
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

// Re-export hardware classes for external use
export { HardwareState, I2CDevice } from './hardware.mjs';
export { HardwareTarget, WebUSBTarget, WebSerialTarget, TeeTarget } from './targets.mjs';
