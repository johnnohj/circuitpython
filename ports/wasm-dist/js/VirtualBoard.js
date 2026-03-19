/*
 * VirtualBoard.js — CircuitPython virtual board supervisor
 *
 * Models the real CircuitPython supervisor lifecycle:
 *   port_init → run_boot_py → supervisor_workflow_start →
 *   Main Loop { run_code_py → wait (autoreload | serial → REPL) }
 *
 * Usage:
 *   import { VirtualBoard } from './VirtualBoard.js';
 *   const board = new VirtualBoard();
 *   board.on('stdout', text => console.log(text));
 *   board.writeFile('code.py', 'import board\nprint(board.LED)');
 *   await board.boot();
 *   // board runs code.py, enters WAITING state
 *   // board.reload() to re-run, board.enterRepl() for REPL
 *
 * The VirtualBoard owns:
 *   - PythonHost (executor workers)
 *   - PythonREPL (lazy, created on enterRepl())
 *   - HardwareSimulator (hw event state machine)
 *   - /circuitpy/ filesystem (in-memory, synced to executor MEMFS)
 */

import { PythonHost }        from './PythonHost.js';
import { PythonREPL }        from './PythonREPL.js';
import { HardwareSimulator } from './HardwareSimulator.js';
import { BLINKA_SHIMS }      from './shims.js';

// ── Board states (mirrors real CircuitPython supervisor states) ──────────────

export const BOARD_STATE = {
    INIT:       'init',
    BOOTING:    'booting',     // running boot.py
    RUNNING:    'running',     // running code.py / main.py
    WAITING:    'waiting',     // code finished, waiting for reload or REPL
    REPL:       'repl',        // interactive REPL active
    SAFE_MODE:  'safe_mode',   // boot.py or hard error — user code skipped
    STOPPED:    'stopped',     // shutdown
};

// Code file search order (matches real CircuitPython)
const CODE_FILE_ORDER = ['code.py', 'code.txt', 'main.py', 'main.txt'];

// ── VirtualBoard ─────────────────────────────────────────────────────────────

export class VirtualBoard {
    /**
     * @param {Object} opts
     * @param {number} opts.heapSize    WASM heap bytes (default 512 KiB)
     * @param {number} opts.timeout     Default code.py timeout ms (default 30000)
     * @param {string} opts.board       Board name (for future board-specific configs)
     */
    constructor(opts = {}) {
        this._state      = BOARD_STATE.INIT;
        this._opts       = {
            heapSize: opts.heapSize || 512 * 1024,
            timeout:  opts.timeout  || 30000,
            board:    opts.board    || 'wasm-simulator',
        };

        this._python     = null;    // PythonHost — created in boot()
        this._repl       = null;    // PythonREPL — created lazily in enterRepl()
        this._hw         = new HardwareSimulator();
        this._listeners  = {};
        this._circuitpy  = {};      // filename → source (in-memory /circuitpy/)
        this._safeMode   = null;    // null or safe mode reason string
        this._lastResult = null;    // last code.py exec result
        this._unsubs     = [];      // event unsubscribe functions for cleanup
    }

    /** Current board state. */
    get state() { return this._state; }

    /** HardwareSimulator instance (for reading pin/display/neopixel state). */
    get hardware() { return this._hw; }

    /** Safe mode reason, or null if not in safe mode. */
    get safeMode() { return this._safeMode; }

    /** Result from last code.py execution, or null. */
    get lastResult() { return this._lastResult; }

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * Boot the virtual board.
     * Initializes workers, installs shims, runs boot.py (if present),
     * then executes code.py and enters WAITING state.
     */
    async boot() {
        // ── port_init ──
        this._python = new PythonHost({
            executors: 1,
            timeout: this._opts.timeout,
        });
        await this._python.init();

        // Install blinka shims (digitalio, board, neopixel, time, displayio)
        this._python.installBlinka();

        // Start hardware event listener
        this._hw.start();

        // Wire PythonHost events to board events
        this._unsubs.push(this._python.on('stdout',   t  => this._emit('stdout', t)));
        this._unsubs.push(this._python.on('stderr',   t  => this._emit('stderr', t)));
        this._unsubs.push(this._python.on('hardware', ev => this._emit('hardware', ev)));

        // Sync /circuitpy/ files to executor MEMFS
        this._syncCircuitPy();

        // ── run_boot_py ──
        this._setState(BOARD_STATE.BOOTING);
        const bootSrc = this._circuitpy['boot.py'];
        if (bootSrc) {
            try {
                const result = await this._python.exec(bootSrc, { timeout: 10000 });
                if (result.stderr) {
                    this._safeMode = 'SAFE_MODE_BOOT_PY_ERROR';
                    this._emit('error', {
                        type: 'safe_mode',
                        reason: this._safeMode,
                        stderr: result.stderr,
                    });
                    this._setState(BOARD_STATE.SAFE_MODE);
                    return;
                }
            } catch (e) {
                this._safeMode = 'SAFE_MODE_BOOT_PY_ERROR';
                this._emit('error', {
                    type: 'safe_mode',
                    reason: this._safeMode,
                    error: e.message,
                });
                this._setState(BOARD_STATE.SAFE_MODE);
                return;
            }
        }

        // ── supervisor_workflow_start ──
        // (BroadcastChannel, register sync, HW simulator already running)

        // ── run_code_py ──
        await this._runCodePy();
    }

    /**
     * Reload: re-sync /circuitpy/ and re-run code.py.
     * Equivalent to Ctrl+D in real CircuitPython REPL or autoreload trigger.
     */
    async reload() {
        if (this._state === BOARD_STATE.REPL && this._repl) {
            this._repl.reset();
        }
        this._syncCircuitPy();
        await this._runCodePy();
    }

    /**
     * Enter REPL mode. Creates a PythonREPL worker if needed.
     * Call sendReplLine() to send input, listen to 'repl_output'/'repl_prompt'.
     */
    async enterRepl() {
        if (!this._repl) {
            this._repl = new PythonREPL();
            await this._repl.init();

            // Install shims in the REPL worker too
            for (const [name, source] of Object.entries(BLINKA_SHIMS)) {
                this._repl.writeFile('/flash/lib/' + name, source);
            }

            // Forward REPL events
            this._repl.on('output', t => this._emit('repl_output', t));
            this._repl.on('error',  t => this._emit('repl_error', t));
            this._repl.on('prompt', p => this._emit('repl_prompt', p));
        }

        this._setState(BOARD_STATE.REPL);
        this._emit('repl_prompt', '>>> ');
    }

    /**
     * Send a line to the REPL. Only valid in REPL state.
     * @param {string} line
     */
    sendReplLine(line) {
        if (this._state !== BOARD_STATE.REPL || !this._repl) {
            throw new Error('Not in REPL mode (state=' + this._state + ')');
        }
        this._repl.sendLine(line);
    }

    /**
     * Exit REPL and reload code.py (Ctrl+D behavior).
     */
    async exitRepl() {
        if (this._state === BOARD_STATE.REPL && this._repl) {
            this._repl.reset();
        }
        await this.reload();
    }

    // ── Filesystem ────────────────────────────────────────────────────────────

    /**
     * Write a file to /circuitpy/.
     * @param {string} name   Filename (e.g. 'code.py', 'lib/mymodule.py')
     * @param {string} content
     */
    writeFile(name, content) {
        this._circuitpy[name] = content;
        // Live-sync to executor if running
        if (this._python) {
            this._python.writeModule('circuitpy/' + name, content);
        }
    }

    /**
     * Read a file from /circuitpy/.
     * @param {string} name
     * @returns {string|null}
     */
    readFile(name) {
        return this._circuitpy[name] ?? null;
    }

    /**
     * List files in /circuitpy/.
     * @returns {string[]}
     */
    listFiles() {
        return Object.keys(this._circuitpy);
    }

    // ── Hardware ──────────────────────────────────────────────────────────────

    /**
     * Write hardware register state (JS → Python direction).
     * Values are synced to the executor's register file via bc_in,
     * picked up by mp_hal_hook() every 64 bytecodes.
     * @param {Object} regs  e.g. { LED: 1, D5: 0, BUTTON: 1 }
     */
    writeRegisters(regs) {
        if (this._python) {
            this._python.writeRegisters(regs);
        }
    }

    // ── Events ────────────────────────────────────────────────────────────────

    /**
     * Subscribe to a board event.
     *
     * Lifecycle: 'state'
     * Output:    'stdout', 'stderr'
     * Hardware:  'hardware'
     * REPL:      'repl_output', 'repl_error', 'repl_prompt'
     * Errors:    'error'
     *
     * @param {string} event
     * @param {Function} cb
     * @returns {Function} Unsubscribe
     */
    on(event, cb) {
        if (!this._listeners[event]) { this._listeners[event] = new Set(); }
        this._listeners[event].add(cb);
        return () => this._listeners[event].delete(cb);
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────

    /** Shut down the board: terminate all workers, release resources. */
    async shutdown() {
        this._setState(BOARD_STATE.STOPPED);
        this._hw.stop();
        this._unsubs.forEach(u => u());
        this._unsubs = [];
        if (this._repl)   { await this._repl.shutdown(); this._repl = null; }
        if (this._python)  { await this._python.shutdown(); this._python = null; }
    }

    // ── Internal ──────────────────────────────────────────────────────────────

    async _runCodePy() {
        this._setState(BOARD_STATE.RUNNING);

        const codeFile = this._findCodeFile();
        if (!codeFile) {
            // No code file found — go straight to waiting (like real CP)
            this._setState(BOARD_STATE.WAITING);
            return;
        }

        try {
            this._lastResult = await this._python.exec(codeFile.source, {
                timeout: this._opts.timeout,
            });
        } catch (e) {
            this._lastResult = { stdout: '', stderr: e.message, aborted: false };
            this._emit('stderr', e.message);
        }

        // Emit result details
        if (this._lastResult?.stdout) {
            // stdout was already emitted via the 'stdout' event during execution
        }
        if (this._lastResult?.stderr) {
            this._emit('stderr', this._lastResult.stderr);
        }

        this._setState(BOARD_STATE.WAITING);
    }

    _findCodeFile() {
        for (const name of CODE_FILE_ORDER) {
            if (this._circuitpy[name]) {
                return { name, source: this._circuitpy[name] };
            }
        }
        return null;
    }

    _syncCircuitPy() {
        if (!this._python) { return; }
        for (const [name, source] of Object.entries(this._circuitpy)) {
            this._python.writeModule('circuitpy/' + name, source);
        }
    }

    _setState(state) {
        if (this._state === state) { return; }
        this._state = state;
        this._emit('state', state);
    }

    _emit(event, data) {
        const cbs = this._listeners[event];
        if (!cbs) { return; }
        cbs.forEach(cb => { try { cb(data); } catch {} });
    }
}
