/*
 * api.js — WASM-dist Public API
 *
 * Initialises Module.vm, Module.state, and Module.dev namespaces after the
 * Emscripten module is created.  Call initializeModuleAPI(Module) once the
 * WASM module is ready.
 *
 * Module.vm   — Python VM init and execution
 * Module.state — Read execution results from /state/result.json
 * Module.dev  — Read/write virtual devices (/dev/)
 * Module.fs   — Thin helpers over Module.FS (write files to /flash/ etc.)
 */

function initializeModuleAPI(Module) {
    const FS = Module.FS;

    // =========================================================================
    // Display framebuffer attachment
    //
    // When a display_refresh bc_out event is drained, read the raw RGB bytes
    // from the MEMFS file written by Python and attach as a Uint8Array.
    // This avoids expensive base64/hex encoding in Python.
    // =========================================================================

    function _attachDisplayPixels(fs, obj) {
        if (obj.type === 'hw' && obj.cmd === 'display_refresh' && obj.fb_path) {
            try {
                // Python VFS root = MEMFS /flash, so fb_path "/_fb_disp1"
                // maps to MEMFS "/flash/_fb_disp1"
                const memfsPath = '/flash' + obj.fb_path;
                const raw = fs.readFile(memfsPath);
                obj.pixels = new Uint8Array(raw);
                delete obj.fb_path;  // no longer needed

                // Also write to OPFS framebuf region for cross-worker access
                // Layout: [0:3] = width(u16 LE) + height(u16 LE), [4:] = RGB data
                if (Module._opfsInitialized && obj.pixels.length > 0) {
                    const w = obj.width || 0, h = obj.height || 0;
                    const hdr = new Uint8Array(4);
                    hdr[0] = w & 0xFF; hdr[1] = (w >> 8) & 0xFF;
                    hdr[2] = h & 0xFF; hdr[3] = (h >> 8) & 0xFF;
                    Module.opfs.write(Module.opfs.FRAMEBUF, 0, hdr);
                    Module.opfs.write(Module.opfs.FRAMEBUF, 4, obj.pixels);
                }
            } catch {}
        }
    }

    // =========================================================================
    // Module.vm — Python Virtual Machine
    // =========================================================================
    Module.vm = {
        _initialized: false,

        /**
         * Initialize the Python VM.
         * @param {Object} opts  { pystackSize?: number (words), heapSize?: number (bytes) }
         */
        init(opts = {}) {
            const { pystackSize = 4 * 1024, heapSize = 512 * 1024 } = opts;
            Module.ccall('mp_js_init', null, ['number', 'number'],
                         [pystackSize, heapSize]);
            this._initialized = true;
        },

        /**
         * Execute Python source code with a timeout.
         * Writes /state/result.json; returns the parsed result.
         * @param {string}  code
         * @param {number}  timeout  milliseconds (default 500)
         * @returns {{ delta, stdout, stderr, aborted, duration_ms, frames }}
         */
        run(code, timeout = 500) {
            const encoded = Module.lengthBytesUTF8(code);
            const buf = Module._malloc(encoded + 1);
            Module.stringToUTF8(code, buf, encoded + 1);
            const out = Module._malloc(3 * 4);
            try {
                Module.ccall('mp_js_run', 'number',
                    ['number', 'number', 'number', 'number'],
                    [buf, encoded, timeout, out]);
            } finally {
                Module._free(buf);
                Module._free(out);
            }
            // Final flush: drain any remaining broadcast ring buffer entries
            Module.ccall('mp_bc_out_flush', 'number', [], []);
            return Module.state.readResult();
        },

        /**
         * Execute Python source synchronously (no timeout, no state delta).
         * Returns the proxy-converted result object.
         */
        exec(code) {
            const encoded = Module.lengthBytesUTF8(code);
            const buf = Module._malloc(encoded + 1);
            Module.stringToUTF8(code, buf, encoded + 1);
            const out = Module._malloc(3 * 4);
            try {
                Module.ccall('mp_js_do_exec', null,
                    ['number', 'number', 'number'],
                    [buf, encoded, out]);
                return proxy_convert_mp_to_js_obj_jsside_with_free(out);
            } finally {
                Module._free(buf);
            }
        },

        /**
         * Import a Python module by name.
         */
        import(name) {
            const out = Module._malloc(3 * 4);
            Module.ccall('mp_js_do_import', null, ['string', 'number'], [name, out]);
            return proxy_convert_mp_to_js_obj_jsside_with_free(out);
        },

        /**
         * Compile Python source to .mpy bytecode, writing to /flash/<name>.mpy.
         * @param {string} name    Module name (e.g. 'mymod') — no .py extension
         * @param {string} src     Python source code
         * @returns {string|null}  MEMFS path to .mpy on success, null on error
         *                         (error printed to /dev/py_stderr)
         */
        compileToMpy(name, src) {
            const nameLen  = Module.lengthBytesUTF8(name);
            const nameBuf  = Module._malloc(nameLen + 1);
            Module.stringToUTF8(name, nameBuf, nameLen + 1);
            const srcLen   = Module.lengthBytesUTF8(src);
            const srcBuf   = Module._malloc(srcLen + 1);
            Module.stringToUTF8(src, srcBuf, srcLen + 1);
            let rc;
            try {
                rc = Module.ccall('mp_js_compile_to_mpy', 'number',
                    ['number', 'number', 'number'],
                    [nameBuf, srcBuf, srcLen]);
            } finally {
                Module._free(nameBuf);
                Module._free(srcBuf);
            }
            return rc === 0 ? '/flash/' + name + '.mpy' : null;
        },

        /**
         * Checkpoint the GC heap and pystack to /mem/heap + /mem/pystack.
         * Also writes /mem/state.json with size metadata.
         */
        checkpoint() {
            Module.ccall('mp_js_checkpoint_vm', null, [], []);
        },

        /**
         * Restore the GC heap and pystack from /mem/ and sweep with gc_collect().
         * The WASM instance must not have been reloaded since checkpoint().
         */
        restore() {
            Module.ccall('mp_js_restore_vm', null, [], []);
        },

        /**
         * Enable or disable the built-in sys.settrace hook.
         * When enabled, trace events are written as NDJSON to /debug/trace.json.
         * @param {boolean} on
         */
        enableTrace(on) {
            Module.ccall('mp_js_enable_trace', null, ['number'], [on ? 1 : 0]);
        },

        /**
         * Read /debug/trace.json and return parsed array of trace event objects.
         * Each object: { event, lineno, file, name }
         * @returns {Array}
         */
        readTrace() {
            try {
                const raw = FS.readFile('/debug/trace.json', { encoding: 'utf8' });
                return raw.trim().split('\n').filter(Boolean).map(line => JSON.parse(line));
            } catch (e) {
                return [];
            }
        },

        /**
         * Check whether a REPL input line needs continuation (incomplete block).
         * Wraps mp_repl_continue_with_input() — returns true if more input expected.
         * @param {string} line  The line typed so far
         * @returns {boolean}
         */
        replContinue(line) {
            const len  = Module.lengthBytesUTF8(line);
            const buf  = Module._malloc(len + 1);
            Module.stringToUTF8(line, buf, len + 1);
            let result;
            try {
                result = Module.ccall('mp_repl_continue_with_input', 'number',
                    ['number'], [buf]);
            } finally {
                Module._free(buf);
            }
            return !!result;
        },

        // ── Step-wise VM execution ───────────────────────────────────────

        /**
         * Compile Python code and prepare for step-wise execution.
         * Call step() repeatedly to execute in batches.
         * @param {string} code    Python source code
         * @returns {boolean}  true if compilation succeeded
         */
        start(code) {
            const len = Module.lengthBytesUTF8(code);
            const buf = Module._malloc(len + 1);
            Module.stringToUTF8(code, buf, len + 1);
            let rc;
            try {
                rc = Module.ccall('mp_vm_start', 'number',
                    ['number', 'number'], [buf, len]);
            } finally {
                Module._free(buf);
            }
            return rc === 0;
        },

        /**
         * Execute one batch of bytecodes.  Set the budget before calling:
         *   Module._vm_step_budget = 256;
         *   const status = Module.vm.step();
         *
         * @param {number} [budget=256]  Branch points to execute before yielding
         * @returns {number}  0=normal completion, 1=yielded (call again), 2=exception
         */
        step(budget) {
            Module._vm_step_budget = budget ?? 256;
            return Module.ccall('mp_vm_step', 'number', [], []);
        },

        /**
         * Run Python code in steps with an async driver.
         * Yields to the event loop between batches.
         *
         * When Python code calls time.sleep(), asyncio.sleep(), or
         * mp_hal_delay_ms(), the VM requests an async delay via
         * libpyasync.js.  This driver honours that delay with a real
         * setTimeout, freeing the browser event loop during sleeps.
         *
         * @param {string} code      Python source
         * @param {Object} [opts]
         * @param {number} [opts.budget=256]   Branch points per step
         * @param {number} [opts.yieldMs=0]    Ms to yield between steps (0 = microtask)
         * @param {number} [opts.timeout=0]    Total timeout in ms (0 = no limit)
         * @param {Function} [opts.onYield]    Callback between steps
         * @returns {Promise<{stdout, stderr, status}>}
         */
        async runStepped(code, opts = {}) {
            const budget  = opts.budget  ?? 256;
            const yieldMs = opts.yieldMs ?? 0;
            const timeout = opts.timeout ?? 0;
            const onYield = opts.onYield ?? null;

            // Clear stdout/stderr capture
            Module._pyStdout = '';
            Module._pyStderr = '';

            const ok = Module.vm.start(code);
            if (!ok) {
                return { stdout: '', stderr: Module._pyStderr || 'compile error', status: 2 };
            }

            // Set deadline if timeout specified
            if (timeout > 0) {
                Module.ccall('mp_js_set_deadline', null, ['number'],
                    [(Date.now() + timeout) | 0]);
            }

            let status;
            const t0 = Date.now();
            for (;;) {
                Module._vm_step_budget = budget;
                status = Module.ccall('mp_vm_step', 'number', [], []);

                if (status !== 1) break;  // 0=done, 2=exception

                // Yield point — run all background tasks (broadcast flush, etc.)
                Module.ccall('mp_tasks_poll', 'number', [], []);

                if (onYield) { onYield(); }

                // Check for async delay requested by Python (time.sleep, asyncio.sleep)
                const asyncDelay = Module.ccall('mp_async_consume_delay', 'number', [], []);
                const delayMs = asyncDelay > 0 ? asyncDelay : yieldMs;

                // Yield to the event loop with the appropriate delay
                await new Promise(r => setTimeout(r, delayMs));

                // Timeout check for stepped mode
                if (timeout > 0 && (Date.now() - t0) >= timeout) {
                    Module.ccall('mp_sched_keyboard_interrupt', null, [], []);
                    // Run one more step to let the interrupt fire
                    Module._vm_step_budget = 1;
                    status = Module.ccall('mp_vm_step', 'number', [], []);
                    break;
                }
            }

            // Clear deadline
            if (timeout > 0) {
                Module.ccall('mp_js_set_deadline', null, ['number'], [0]);
            }

            // Final flush
            Module.ccall('mp_bc_out_flush', 'number', [], []);

            const stdout = Module._pyStdout || '';
            const stderr = Module._pyStderr || '';
            Module._pyStdout = '';
            Module._pyStderr = '';
            return { stdout, stderr, status };
        },

        /**
         * Register a JavaScript object as an importable Python module.
         */
        registerModule(name, jsObj) {
            const val = Module._malloc(3 * 4);
            proxy_convert_js_to_mp_obj_jsside(jsObj, val);
            Module.ccall('mp_js_register_js_module', null,
                ['string', 'number'], [name, val]);
            Module._free(val);
        },
    };

    // =========================================================================
    // Module.hw — Hardware register file
    //
    // 256 × 16-bit registers in WASM linear memory.  Pin names map to
    // addresses (D0=0x00..D13=0x0D, LED=0x0E, BUTTON=0x0F, A0=0x10..A5=0x15).
    // JS writes registers; Python reads them via _blinka.read_reg().
    // =========================================================================
    Module.hw = {
        /** Register address constants (mirror modblinka.c) */
        REG_D0: 0x00, REG_D1: 0x01, REG_D2: 0x02, REG_D3: 0x03,
        REG_D4: 0x04, REG_D5: 0x05, REG_D6: 0x06, REG_D7: 0x07,
        REG_D8: 0x08, REG_D9: 0x09, REG_D10: 0x0A, REG_D11: 0x0B,
        REG_D12: 0x0C, REG_D13: 0x0D, REG_LED: 0x0E, REG_BUTTON: 0x0F,
        REG_A0: 0x10, REG_A1: 0x11, REG_A2: 0x12, REG_A3: 0x13,
        REG_A4: 0x14, REG_A5: 0x15,
        REG_FLAGS: 0xF0,

        /** Map pin name string to register address. */
        pinToReg(pin) {
            const map = {
                LED: 0x0E, BUTTON: 0x0F,
                D0:0,D1:1,D2:2,D3:3,D4:4,D5:5,D6:6,D7:7,
                D8:8,D9:9,D10:0xA,D11:0xB,D12:0xC,D13:0xD,
                A0:0x10,A1:0x11,A2:0x12,A3:0x13,A4:0x14,A5:0x15,
            };
            return map[pin] ?? -1;
        },

        /** Read a single 16-bit register. */
        readReg(addr) {
            return Module.ccall('hw_reg_read', 'number', ['number'], [addr]);
        },

        /** Write a single 16-bit register. */
        writeReg(addr, value) {
            Module.ccall('hw_reg_write', null, ['number', 'number'], [addr, value]);
        },

        /**
         * Batch-write registers from a plain object { pinName: value }.
         * Also writes to /dev/bc_in so Python's sync_registers() picks them up.
         * @param {Object} regs  e.g. { LED: 1, D5: 0, A0: 512 }
         */
        writeRegisters(regs) {
            const json = JSON.stringify(regs);
            // Write directly to C register file (immediate for same-thread reads)
            const len  = Module.lengthBytesUTF8(json);
            const buf  = Module._malloc(len + 1);
            Module.stringToUTF8(json, buf, len + 1);
            Module.ccall('hw_reg_write_batch', null, ['number', 'number'], [buf, len]);
            Module._free(buf);
            // Also write to /dev/bc_in for Python's sync_registers() to read
            FS.writeFile('/dev/bc_in', json);
        },

        /** Enable OPFS dual-write for cross-worker register sharing. */
        enableOpfs() {
            Module._hw_reg_enable_opfs();
        },

        /** Pull register state from OPFS into the local cache. */
        syncFromOpfs() {
            Module._hw_reg_sync_from_opfs();
        },

        /** Push local register cache to OPFS. */
        syncToOpfs() {
            Module._hw_reg_sync_to_opfs();
        },
    };

    // =========================================================================
    // Module.opfs — OPFS region access
    // =========================================================================
    Module.opfs = {
        /** Initialize OPFS regions (async — uses OPFS in browser, memory in Node). */
        async init() {
            await Module._opfs_init();
            // Enable dual-write from register file to OPFS
            Module._hw_reg_enable_opfs();
        },

        /** Get the current storage mode ('opfs' or 'memory'). */
        get mode() { return Module._opfsMode || 'uninitialized'; },

        /**
         * Read bytes from an OPFS region.
         * @param {number} regionId — OPFS_REGION_* constant (0-4)
         * @param {number} offset
         * @param {number} len
         * @returns {Uint8Array}
         */
        read(regionId, offset, len) {
            const ptr = Module._malloc(len);
            const n = Module._opfs_read(regionId, offset, ptr, len);
            const result = new Uint8Array(n);
            result.set(Module.HEAPU8.subarray(ptr, ptr + n));
            Module._free(ptr);
            return result;
        },

        /**
         * Write bytes to an OPFS region.
         * @param {number} regionId
         * @param {number} offset
         * @param {Uint8Array} data
         * @returns {number} bytes written
         */
        write(regionId, offset, data) {
            const ptr = Module._malloc(data.length);
            Module.HEAPU8.set(data, ptr);
            const n = Module._opfs_write(regionId, offset, ptr, data.length);
            Module._free(ptr);
            return n;
        },

        /** Flush an OPFS region to persistent storage. */
        flush(regionId) {
            Module._opfs_flush(regionId);
        },

        /** Region ID constants. */
        REGISTERS: 0,
        SENSORS: 1,
        EVENTS: 2,
        FRAMEBUF: 3,
        CONTROL: 4,
    };

    // =========================================================================
    // Module.state — Execution state / result
    // =========================================================================
    Module.state = {
        /**
         * Read and parse /state/result.json.
         * Returns { delta, stdout, stderr, aborted, duration_ms, frames }.
         */
        readResult() {
            try {
                const raw = FS.readFile('/state/result.json', { encoding: 'utf8' });
                return JSON.parse(raw);
            } catch (e) {
                return { delta: {}, stdout: '', stderr: String(e),
                         aborted: false, duration_ms: 0, frames: [] };
            }
        },

        /**
         * Read /state/snapshot.json (pre-run globals baseline).
         */
        readSnapshot() {
            try {
                return JSON.parse(
                    FS.readFile('/state/snapshot.json', { encoding: 'utf8' }));
            } catch (e) {
                return {};
            }
        },
    };

    // =========================================================================
    // Module.dev — Virtual device I/O
    // =========================================================================
    Module.dev = {
        /**
         * Read a virtual device file.
         * @param {string} name  e.g. 'stdout', 'bc_in', 'time'
         * @returns {string}
         */
        read(name) {
            try {
                return FS.readFile('/dev/' + name, { encoding: 'utf8' });
            } catch (e) {
                return '';
            }
        },

        /**
         * Write to a virtual device file (replaces contents).
         */
        write(name, content) {
            FS.writeFile('/dev/' + name, content);
        },

        /**
         * Append to a virtual device file.
         */
        append(name, content) {
            try {
                const existing = FS.readFile('/dev/' + name, { encoding: 'utf8' });
                FS.writeFile('/dev/' + name, existing + content);
            } catch (e) {
                FS.writeFile('/dev/' + name, content);
            }
        },

        /**
         * Write a JSON line to /dev/bc_in so Python can read it.
         * Equivalent to calling the C vdev_bc_in_write() export.
         */
        sendToPython(msg) {
            const line = JSON.stringify(msg) + '\n';
            this.append('bc_in', line);
        },

        /**
         * Raise a KeyboardInterrupt in the running Python VM.
         */
        interrupt() {
            Module.ccall('mp_sched_keyboard_interrupt', null, [], []);
        },
    };

    // =========================================================================
    // Module.fs — MEMFS file helpers
    // =========================================================================
    Module.fs = {
        /**
         * Write a Python source file to /flash/.
         * Creates intermediate directories as needed.
         */
        writeModule(relativePath, source) {
            const full = relativePath.startsWith('/') ? relativePath
                       : '/flash/' + relativePath;
            const dir = full.substring(0, full.lastIndexOf('/'));
            this._mkdirp(dir);
            FS.writeFile(full, source);
        },

        /**
         * Read a file from MEMFS.
         */
        read(path) {
            return FS.readFile(path, { encoding: 'utf8' });
        },

        /**
         * Write raw content to a MEMFS path.
         */
        write(path, content) {
            FS.writeFile(path, content);
        },

        /**
         * Mount an IndexedDB-backed filesystem at /flash/circuitpy (browser)
         * and load its contents into MEMFS.
         *
         * In the browser this mounts IDBFS (Emscripten's IndexedDB FS) and
         * calls FS.syncfs(true) to populate MEMFS from the database.
         * In Node.js (no IndexedDB) this is a no-op that returns a resolved Promise.
         *
         * The IDB database name is derived from the mount path by Emscripten.
         * @returns {Promise<void>}
         */
        mountIDBFS() {
            const mountPath = '/flash/circuitpy';
            // IDBFS requires IndexedDB, which is only available in browser environments.
            // In Node.js, globalThis.indexedDB is undefined — stay with MEMFS.
            if (typeof globalThis.indexedDB === 'undefined') {
                return Promise.resolve();
            }
            try {
                FS.mount(Module.IDBFS, {}, mountPath);
            } catch (e) {
                if (e.errno !== 20 /* EEXIST — already mounted */) {
                    return Promise.reject(e);
                }
            }
            return new Promise((resolve, reject) => {
                FS.syncfs(true, (err) => err ? reject(err) : resolve());
            });
        },

        /**
         * Sync the mounted IDBFS filesystem.
         * @param {boolean} toDB  true = MEMFS→IndexedDB (save), false = IndexedDB→MEMFS (load)
         * @returns {Promise<void>}
         */
        syncfs(toDB) {
            // No-op if not in a browser with IndexedDB
            if (typeof globalThis.indexedDB === 'undefined') {
                return Promise.resolve();
            }
            return new Promise((resolve, reject) => {
                try {
                    FS.syncfs(toDB, (err) => err ? reject(err) : resolve());
                } catch (e) {
                    resolve();
                }
            });
        },

        _mkdirp(dir) {
            if (!dir || dir === '/') { return; }
            try { FS.mkdir(dir); } catch (e) {
                if (e.errno !== 20) {
                    this._mkdirp(dir.substring(0, dir.lastIndexOf('/')));
                    try { FS.mkdir(dir); } catch (e2) {}
                }
            }
        },
    };

    // =========================================================================
    // Module.broadcast — BroadcastChannel IPC (libpybroadcast.js)
    // =========================================================================
    Module.broadcast = {
        /** Flush all pending outgoing messages to BroadcastChannel. */
        flush() {
            return Module.ccall('mp_bc_out_flush', 'number', [], []);
        },

        /** Enqueue a JSON object for broadcast (Python→JS direction). */
        send(obj) {
            const json = JSON.stringify(obj);
            const len  = Module.lengthBytesUTF8(json);
            const buf  = Module._malloc(len + 1);
            Module.stringToUTF8(json, buf, len + 1);
            try {
                Module.ccall('mp_bc_out_enqueue', null, ['number', 'number'], [buf, len]);
            } finally {
                Module._free(buf);
            }
        },

        /** Write a JSON object to the incoming buffer (JS→Python direction). */
        sendToPython(obj) {
            const json = JSON.stringify(obj);
            const len  = Module.lengthBytesUTF8(json);
            const buf  = Module._malloc(len + 1);
            Module.stringToUTF8(json, buf, len + 1);
            try {
                Module.ccall('mp_bc_in_write', null, ['number', 'number'], [buf, len]);
            } finally {
                Module._free(buf);
            }
        },

        /** Check if there is pending incoming data. */
        hasPending() {
            return !!Module.ccall('mp_bc_in_pending', 'number', [], []);
        },
    };

    // =========================================================================
    // Module.tasks — Background task queue (libpytasks.js)
    // =========================================================================
    Module.tasks = {
        /** Run all registered background tasks. Returns count executed. */
        poll() {
            return Module.ccall('mp_tasks_poll', 'number', [], []);
        },

        /**
         * Register a JS background task.
         * @param {string} name       Unique task name
         * @param {Function} fn       Function to call on each poll
         * @param {Object} [opts]
         * @param {number} [opts.interval=0]  Minimum ms between invocations (0=every poll)
         */
        register(name, fn, opts = {}) {
            Module.ccall('mp_tasks_init', null, [], []);  // ensure initialized
            Module._taskRegistry[name] = {
                fn,
                interval: opts.interval ?? 0,
                lastRun: 0,
            };
        },

        /** Unregister a background task. */
        unregister(name) {
            if (Module._taskRegistry) {
                delete Module._taskRegistry[name];
            }
        },

        /** Request the VM to yield at the next opportunity. */
        requestYield() {
            Module.ccall('mp_tasks_request_yield', null, [], []);
        },
    };

    // =========================================================================
    // Module.sensors — Catalog-driven sensor simulation
    //
    // Registers virtual I2C sensors that respond to busio.I2C transactions.
    // Device handlers intercept bc_out events synchronously during flush
    // and write responses to MEMFS mailbox files for Python to read.
    // =========================================================================
    Module.sensors = {
        _sim: null,
        _devices: new Map(),  // address → handler

        /**
         * Load a sensor catalog (parsed JSON).
         */
        loadCatalog(catalog) {
            // Lazy import: SensorSimulator may not be available in all contexts
            const { SensorSimulator } = Module.sensors._SensorSimulator
                ? { SensorSimulator: Module.sensors._SensorSimulator }
                : { SensorSimulator: class { constructor() { throw new Error('SensorSimulator not loaded'); } } };
            Module.sensors._sim = new SensorSimulator(catalog);
        },

        /**
         * Register SensorSimulator class (called from external code).
         */
        setSensorSimulatorClass(cls) {
            Module.sensors._SensorSimulator = cls;
        },

        /**
         * Add a simulated sensor.
         * @param {string} type       Catalog key (e.g. 'bme280')
         * @param {Object} [values]   Initial values (e.g. { temperature: 25 })
         * @param {Object} [opts]     { address, busId }
         * @returns {string}          Instance ID
         */
        add(type, values, opts) {
            if (!Module.sensors._sim) throw new Error('Load a catalog first');
            const sim = Module.sensors._sim;
            // Create a lightweight hw proxy for addSensor
            const hwProxy = {
                addI2CDevice: (busId, address, handler) => {
                    Module.sensors._devices.set(address, handler);
                },
            };
            return sim.addSensor(hwProxy, type, values, opts);
        },

        /**
         * Update a sensor value at runtime.
         */
        setValue(id, capability, value) {
            Module.sensors._sim?.setValue(id, capability, value);
        },

        getValue(id, capability) {
            return Module.sensors._sim?.getValue(id, capability);
        },

        get instances() {
            return Module.sensors._sim?.instances ?? {};
        },
    };

    // bc_out interceptor: route I2C/SPI events to device handlers,
    // write responses to MEMFS mailbox files
    if (!Module._bcOutInterceptors) Module._bcOutInterceptors = [];
    Module._bcOutInterceptors.push((obj) => {
        if (obj.type !== 'hw') return;
        const devices = Module.sensors._devices;

        if ((obj.cmd === 'i2c_read' || obj.cmd === 'i2c_write' ||
             obj.cmd === 'i2c_write_read') && devices.has(obj.addr)) {
            const handler = devices.get(obj.addr);
            const response = handler(obj.cmd, obj);
            if (response instanceof Uint8Array) {
                try { FS.writeFile('/flash/i2c_response', response); } catch {}
                // Also write to OPFS sensors region for cross-worker access
                // Layout: [0:1] = response length (u16 LE), [2:2+len] = data
                if (Module._opfsInitialized) {
                    const hdr = new Uint8Array(2);
                    hdr[0] = response.length & 0xFF;
                    hdr[1] = (response.length >> 8) & 0xFF;
                    Module.opfs.write(Module.opfs.SENSORS, 0, hdr);
                    Module.opfs.write(Module.opfs.SENSORS, 2, response);
                }
            } else if (response === true) {
                // Device present but no data response
            }

            // Binary encoding handled by _bp_encode_hw_event in mp_bc_out_flush
        }

        if (obj.cmd === 'i2c_scan') {
            // Return list of registered addresses
            const addrs = [...devices.keys()].filter(k => typeof k === 'number');
            try { FS.writeFile('/flash/i2c_response', JSON.stringify(addrs)); } catch {}
        }
    });

    // Register the built-in 'register_sync' task (sync hardware registers)
    Module.tasks.register('register_sync', () => {
        try {
            Module.ccall('hw_reg_sync_from_bc_in', null, [], []);
        } catch {}
    });

    // =========================================================================
    // Module.gc — GC heap as addressable resource
    // =========================================================================
    Module.gc = {
        stats() {
            return {
                used:  Module.ccall('mp_gc_pool_used',  'number', [], []),
                total: Module.ccall('mp_gc_pool_total', 'number', [], []),
                free:  Module.ccall('mp_gc_pool_free',  'number', [], []),
            };
        },
        snapshot() {
            const addr = Module.ccall('mp_gc_heap_addr', 'number', [], []);
            const size = Module.ccall('mp_gc_heap_size', 'number', [], []);
            return new Uint8Array(Module.HEAPU8.buffer.slice(addr, addr + size));
        },
        restore(buf) {
            const addr = Module.ccall('mp_gc_heap_addr', 'number', [], []);
            Module.HEAPU8.set(new Uint8Array(buf), addr);
        },
        snapshotStateCtx() {
            const addr = Module.ccall('mp_state_ctx_addr', 'number', [], []);
            const size = Module.ccall('mp_state_ctx_size', 'number', [], []);
            return new Uint8Array(Module.HEAPU8.buffer.slice(addr, addr + size));
        },
        restoreStateCtx(buf) {
            const addr = Module.ccall('mp_state_ctx_addr', 'number', [], []);
            Module.HEAPU8.set(new Uint8Array(buf), addr);
        },
        checkpoint() {
            return { heap: Module.gc.snapshot(), stateCtx: Module.gc.snapshotStateCtx() };
        },
        restoreCheckpoint(cp) {
            Module.gc.restore(cp.heap);
            Module.gc.restoreStateCtx(cp.stateCtx);
        },
        blobUrl() {
            const blob = new Blob([Module.gc.snapshot()], { type: 'application/octet-stream' });
            return URL.createObjectURL(blob);
        },
    };

    // =========================================================================
    // Binary event encoder — maps JSON hw events to binary protocol
    //
    // Called from mp_bc_out_flush (libpybroadcast.js) for every hw event.
    // Runs at JS level, zero Python call depth. Writes to the C events ring
    // via Module._bp_events_write().
    // =========================================================================
    const BP = { GPIO: 1, ANALOG: 2, PWM: 3, NEOPIXEL: 4, I2C: 5, SPI: 6, DISPLAY: 7, SLEEP: 8 };
    const CMD_MAP = {
        'gpio_init': [BP.GPIO, 1], 'gpio_write': [BP.GPIO, 2], 'gpio_read': [BP.GPIO, 3], 'gpio_deinit': [BP.GPIO, 4],
        'analog_init': [BP.ANALOG, 1], 'analog_write': [BP.ANALOG, 2], 'analog_read': [BP.ANALOG, 3], 'analog_deinit': [BP.ANALOG, 4],
        'pwm_init': [BP.PWM, 1], 'pwm_update': [BP.PWM, 2], 'pwm_deinit': [BP.PWM, 3],
        'neo_init': [BP.NEOPIXEL, 1], 'neo_write': [BP.NEOPIXEL, 2], 'neo_deinit': [BP.NEOPIXEL, 3],
        'i2c_init': [BP.I2C, 1], 'i2c_scan': [BP.I2C, 2], 'i2c_read': [BP.I2C, 3], 'i2c_write': [BP.I2C, 4], 'i2c_write_read': [BP.I2C, 5], 'i2c_deinit': [BP.I2C, 6],
        'spi_init': [BP.SPI, 1], 'spi_configure': [BP.SPI, 2], 'spi_write': [BP.SPI, 3], 'spi_transfer': [BP.SPI, 4], 'spi_deinit': [BP.SPI, 5],
        'display_init': [BP.DISPLAY, 1], 'display_refresh': [BP.DISPLAY, 2], 'display_deinit': [BP.DISPLAY, 3],
        'time_sleep': [BP.SLEEP, 1],
    };

    // Pin name → register address for binary encoding
    const PIN_ADDR = {};
    for (let i = 0; i <= 13; i++) PIN_ADDR['D' + i] = i;
    PIN_ADDR['LED'] = 0x0E; PIN_ADDR['BUTTON'] = 0x0F;
    for (let i = 0; i <= 5; i++) PIN_ADDR['A' + i] = 0x10 + i;

    Module._bp_encode_hw_event = function(obj) {
        const mapping = CMD_MAP[obj.cmd];
        if (!mapping) return; // unknown command, skip
        const [type, sub] = mapping;

        let payload;
        switch (type) {
            case BP.GPIO: {
                // bp_gpio_t: pin(u8) dir(u8) pull(u8) pad(u8) value(u16 LE)
                const pin = (typeof obj.pin === 'string') ? (PIN_ADDR[obj.pin] ?? 0) : (obj.pin ?? 0);
                const dir = obj.direction === 'output' ? 1 : 0;
                const pull = obj.pull === 'up' ? 1 : obj.pull === 'down' ? 2 : 0;
                const val = obj.value ? 1 : 0;
                payload = new Uint8Array([pin, dir, pull, 0, val & 0xFF, (val >> 8) & 0xFF]);
                break;
            }
            case BP.ANALOG: {
                const pin = (typeof obj.pin === 'string') ? (PIN_ADDR[obj.pin] ?? 0) : (obj.pin ?? 0);
                const dir = obj.direction === 'output' ? 1 : 0;
                const val = obj.value ?? 0;
                payload = new Uint8Array([pin, dir, val & 0xFF, (val >> 8) & 0xFF]);
                break;
            }
            case BP.PWM: {
                const pin = (typeof obj.pin === 'string') ? (PIN_ADDR[obj.pin] ?? 0) : (obj.pin ?? 0);
                const duty = obj.duty_cycle ?? 0;
                const freq = obj.frequency ?? 0;
                payload = new Uint8Array(8);
                const dv = new DataView(payload.buffer);
                dv.setUint8(0, pin);
                dv.setUint8(1, 0);
                dv.setUint16(2, duty, true);
                dv.setUint32(4, freq, true);
                break;
            }
            case BP.NEOPIXEL: {
                const pin = (typeof obj.pin === 'string') ? (PIN_ADDR[obj.pin] ?? 0) : (obj.pin ?? 0);
                const order = obj.order ?? 0;
                const count = obj.n ?? 0;
                // Header: pin(u8) order(u8) count(u16 LE)
                const hdr = new Uint8Array([pin, order, count & 0xFF, (count >> 8) & 0xFF]);
                if (obj.pixels instanceof Uint8Array) {
                    // neo_write: header + pixel data
                    payload = new Uint8Array(4 + obj.pixels.length);
                    payload.set(hdr);
                    payload.set(obj.pixels, 4);
                } else {
                    payload = hdr;
                }
                break;
            }
            case BP.I2C: {
                // Already handled by the sensor interceptor for device events.
                // Encode non-device events (init, deinit, scan) here.
                const addr = obj.addr ?? 0;
                const len = obj.len ?? obj.read_len ?? 0;
                const dataArr = obj.data ?? [];
                payload = new Uint8Array(4 + dataArr.length);
                payload[0] = 0; // bus id
                payload[1] = addr;
                payload[2] = len & 0xFF;
                payload[3] = (len >> 8) & 0xFF;
                for (let i = 0; i < dataArr.length; i++) payload[4 + i] = dataArr[i] & 0xFF;
                break;
            }
            case BP.SPI: {
                const len = obj.len ?? 0;
                const dataArr = obj.data ?? [];
                payload = new Uint8Array(4 + dataArr.length);
                payload[0] = 0;
                payload[1] = 0;
                payload[2] = len & 0xFF;
                payload[3] = (len >> 8) & 0xFF;
                for (let i = 0; i < dataArr.length; i++) payload[4 + i] = dataArr[i] & 0xFF;
                break;
            }
            case BP.DISPLAY: {
                const id = obj.id ?? 0;
                const w = obj.width ?? 0;
                const h = obj.height ?? 0;
                payload = new Uint8Array(10);
                const dv = new DataView(payload.buffer);
                dv.setUint8(0, typeof id === 'number' ? id : 0);
                dv.setUint8(1, 0);
                dv.setUint16(2, w, true);
                dv.setUint16(4, h, true);
                dv.setUint32(6, 0, true); // fb_offset (0 = start of framebuf region)
                break;
            }
            case BP.SLEEP: {
                payload = new Uint8Array(4);
                new DataView(payload.buffer).setUint32(0, obj.ms ?? 0, true);
                break;
            }
            default:
                return;
        }

        // Write to C events ring
        const ptr = Module._malloc(payload.length);
        Module.HEAPU8.set(payload, ptr);
        Module._bp_events_write(type, sub, ptr, payload.length);
        Module._free(ptr);
    };

    // =========================================================================
    // Module.events — Binary event ring buffer
    // =========================================================================
    Module.events = {
        /** Check if there are pending binary events. */
        pending() {
            return !!Module._bp_events_pending();
        },

        /**
         * Read the next binary event message.
         * @returns {Uint8Array|null} — complete message (header + payload), or null
         */
        read() {
            const maxSize = 1024;
            const ptr = Module._malloc(maxSize);
            const n = Module._bp_events_read(ptr, maxSize);
            if (n === 0) { Module._free(ptr); return null; }
            const msg = new Uint8Array(n);
            msg.set(Module.HEAPU8.subarray(ptr, ptr + n));
            Module._free(ptr);
            return msg;
        },

        /**
         * Drain all pending events, returning raw Uint8Array messages.
         * @returns {Array<Uint8Array>}
         */
        drain() {
            const msgs = [];
            let msg;
            while ((msg = this.read()) !== null) {
                msgs.push(msg);
            }
            return msgs;
        },

        /**
         * Write a binary event to the ring (from JS side).
         * @param {number} type — BP_TYPE_* constant
         * @param {number} sub — BP_SUB_* constant
         * @param {Uint8Array} payload
         */
        write(type, sub, payload) {
            const ptr = Module._malloc(payload.length);
            Module.HEAPU8.set(payload, ptr);
            Module._bp_events_write(type, sub, ptr, payload.length);
            Module._free(ptr);
        },
    };

    // =========================================================================
    // Module.pystack — Pystack as addressable resource
    // =========================================================================
    Module.pystack = {
        stats() {
            const total = Module.ccall('mp_pystack_get_size', 'number', [], []);
            const used  = Module.ccall('mp_pystack_get_used', 'number', [], []);
            return { used, total, free: total - used };
        },
        snapshot() {
            const addr = Module.ccall('mp_pystack_get_addr', 'number', [], []);
            const size = Module.ccall('mp_pystack_get_size', 'number', [], []);
            return new Uint8Array(Module.HEAPU8.buffer.slice(addr, addr + size));
        },
        restore(buf) {
            const addr = Module.ccall('mp_pystack_get_addr', 'number', [], []);
            Module.HEAPU8.set(new Uint8Array(buf), addr);
        },
    };
}

// ES module export (used by worker scripts importing api.js directly)
export { initializeModuleAPI };
