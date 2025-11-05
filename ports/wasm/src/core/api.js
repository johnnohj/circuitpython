/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2023-2024 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Options:
// - pystack: size in words of the MicroPython Python stack.
// - heapsize: size in bytes of the MicroPython GC heap.
// - url: location to load `micropython.mjs`.
// - stdin: function to return input characters.
// - stdout: function that takes one argument, and is passed lines of stdout
//   output as they are produced.  By default this is handled by Emscripten
//   and in a browser goes to console, in node goes to process.stdout.write.
// - stderr: same behaviour as stdout but for error output.
// - linebuffer: whether to buffer line-by-line to stdout/stderr.
// - verbose: whether to log infrastructure messages (init). Default: false
// - autoRun: whether to automatically run boot.py and code.py on load. Default: false
// CIRCUITPY_CHANGE: export function name change


function initCooperativeYielding(Module) {
    if (!Module.tickInterval) {
        Module.tickInterval = setInterval(() => {
            if (Module._supervisor_tick_from_js) {
                Module._supervisor_tick_from_js();
            }
        }, 1000);
    }
    
    Module.getYieldStats = function() {
        if (!Module._wasm_get_yield_count) return null;
        return {
            yieldCount: Module._wasm_get_yield_count(),
            bytecodeCount: Module._wasm_get_bytecode_count(),
            lastYieldTime: Module._wasm_get_last_yield_time(),
            yieldState: Module._wasm_get_yield_state()
        };
    };
}

export async function loadCircuitPython(options) {
    const { pystack, heapsize, url, stdin, stdout, stderr, linebuffer, verbose, autoRun, filesystem } =
        Object.assign(
            { pystack: 2 * 1024, heapsize: 1024 * 1024, linebuffer: true, verbose: false, autoRun: false, filesystem: 'memory' },
            options,
        );
    let Module = {};
    Module.locateFile = (path, scriptDirectory) =>
        url || scriptDirectory + path;
    Module._textDecoder = new TextDecoder();
    if (stdin !== undefined) {
        Module.stdin = stdin;
    }
    if (stdout !== undefined) {
        if (linebuffer) {
            Module._stdoutBuffer = [];
            Module.stdout = (c) => {
                if (c === 10) {
                    stdout(
                        Module._textDecoder.decode(
                            new Uint8Array(Module._stdoutBuffer),
                        ),
                    );
                    Module._stdoutBuffer = [];
                } else {
                    Module._stdoutBuffer.push(c);
                }
            };
        } else {
            Module.stdout = (c) => stdout(new Uint8Array([c]));
        }
    }
    if (stderr !== undefined) {
        if (linebuffer) {
            Module._stderrBuffer = [];
            Module.stderr = (c) => {
                if (c === 10) {
                    stderr(
                        Module._textDecoder.decode(
                            new Uint8Array(Module._stderrBuffer),
                        ),
                    );
                    Module._stderrBuffer = [];
                } else {
                    Module._stderrBuffer.push(c);
                }
            };
        } else {
            Module.stderr = (c) => stderr(new Uint8Array([c]));
        }
    }
    // CIRCUITPY-CHANGE: Use CircuitPython module name
    Module = await _createCircuitPythonModule(Module);
    globalThis.Module = Module;
    proxy_js_init();
    const pyimport = (name) => {
        const value = Module._malloc(3 * 4);
        Module.ccall(
            "mp_js_do_import",
            "null",
            ["string", "pointer"],
            [name, value],
        );
        return proxy_convert_mp_to_js_obj_jsside_with_free(value);
    };

    initCooperativeYielding(Module);
    
    Module.ccall(
        "mp_js_init",
        "null",
        ["number", "number"],
        [pystack, heapsize],
    );
    Module.ccall("proxy_c_init", "null", [], []);

    // CIRCUITPY-CHANGE: Initialize minimal board controllers for hardware emulation
    // These provide the virtual hardware objects that tests expect
    // Full controller classes are in src/controllers/*.js - these are lightweight stubs

    // Pin class with onChange callback support
    class Pin {
        constructor(number) {
            this.number = number;
            this.changeCallbacks = [];
        }
        onChange(callback) {
            this.changeCallbacks.push(callback);
        }
        _notifyChange(property, value) {
            for (const cb of this.changeCallbacks) {
                try { cb({ property, value }); } catch (e) { console.error('Pin callback error:', e); }
            }
        }
    }

    // GPIO Controller
    const pins = new Map();
    const gpioController = {
        getPin(number) {
            if (!pins.has(number)) {
                pins.set(number, new Pin(number));
            }
            return pins.get(number);
        },
        getAllPins() { return pins; }
    };

    // Bus class for I2C/SPI with callback support
    class Bus {
        constructor(index, type) {
            this.index = index;
            this.type = type;
            this.probeCallbacks = [];
            this.transactionCallbacks = [];
        }
        onProbe(callback) {
            this.probeCallbacks.push(callback);
        }
        onTransaction(callback) {
            this.transactionCallbacks.push(callback);
        }
        _notifyProbe(data) {
            for (const cb of this.probeCallbacks) {
                try { cb(data); } catch (e) { console.error('Probe callback error:', e); }
            }
        }
        _notifyTransaction(data) {
            for (const cb of this.transactionCallbacks) {
                try { cb(data); } catch (e) { console.error('Transaction callback error:', e); }
            }
        }
    }

    // I2C Controller
    const i2cBuses = new Map();
    const i2cController = {
        getBus(index) {
            if (!i2cBuses.has(index)) {
                i2cBuses.set(index, new Bus(index, 'i2c'));
            }
            return i2cBuses.get(index);
        },
        buses: i2cBuses
    };

    // SPI Controller
    const spiBuses = new Map();
    const spiController = {
        getBus(index) {
            if (!spiBuses.has(index)) {
                spiBuses.set(index, new Bus(index, 'spi'));
            }
            return spiBuses.get(index);
        },
        buses: spiBuses
    };

    Module._circuitPythonBoard = {
        gpio: gpioController,
        i2c: i2cController,
        spi: spiController,
    };

    if (verbose) {
        console.log('[CircuitPython] Board controllers initialized');
    }

    // CIRCUITPY-CHANGE: Initialize persistent filesystem if requested
    let persistentFS = null;
    if (filesystem === 'indexeddb') {
        try {
            // CircuitPythonFilesystem is available globally after concatenation
            // (filesystem.js is concatenated into the .mjs file)
            if (typeof CircuitPythonFilesystem !== 'undefined') {
                persistentFS = new CircuitPythonFilesystem(verbose);
                await persistentFS.init();

                // Sync files from IndexedDB to VFS before running any code
                await persistentFS.syncToVFS(Module);

                if (verbose) {
                    console.log('[CircuitPython] Persistent filesystem initialized');
                }
            } else {
                throw new Error('CircuitPythonFilesystem not available');
            }
        } catch (e) {
            if (verbose) {
                console.warn('[CircuitPython] Persistent filesystem initialization failed:', e);
            }
        }
    }

    // Helper function to run a file if it exists
    const runFile = (filepath) => {
        try {
            if (Module.FS.analyzePath(filepath).exists) {
                const content = Module.FS.readFile(filepath, { encoding: 'utf8' });
                const len = Module.lengthBytesUTF8(content);
                const buf = Module._malloc(len + 1);
                Module.stringToUTF8(content, buf, len + 1);
                const value = Module._malloc(3 * 4);
                Module.ccall(
                    "mp_js_do_exec",
                    "number",
                    ["pointer", "number", "pointer"],
                    [buf, len, value],
                );
                Module._free(buf);
                const ret = proxy_convert_mp_to_js_obj_jsside_with_free(value);
                return ret;
            }
            return null;
        } catch (error) {
            if (verbose) {
                console.error(`[CircuitPython] Error running ${filepath}:`, error);
            }
            throw error;
        }
    };

    // Run CircuitPython boot workflow (boot.py then code.py)
    const runWorkflow = () => {
        try {
            // Run boot.py if it exists
            if (Module.FS.analyzePath('/boot.py').exists) {
                if (verbose) {
                    console.log('[CircuitPython] Running /boot.py');
                }
                runFile('/boot.py');
            }

            // Run code.py if it exists
            if (Module.FS.analyzePath('/code.py').exists) {
                if (verbose) {
                    console.log('[CircuitPython] Running /code.py');
                }
                runFile('/code.py');
            } else if (verbose) {
                console.log('[CircuitPython] No code.py found');
            }
        } catch (error) {
            if (verbose) {
                console.error('[CircuitPython] Workflow error:', error);
            }
            throw error;
        }
    };

    // Helper to save a file to both VFS and IndexedDB
    const saveFile = async (filepath, content) => {
        // Write to VFS
        Module.FS.writeFile(filepath, content);

        // Also persist to IndexedDB if available
        if (persistentFS) {
            await persistentFS.writeFile(filepath, content);
        }
    };

    // Helper to save binary data from various sources
    const saveBinaryFile = async (filepath, data) => {
        let uint8Array;

        // Convert different data types to Uint8Array
        if (data instanceof Uint8Array) {
            uint8Array = data;
        } else if (data instanceof ArrayBuffer) {
            uint8Array = new Uint8Array(data);
        } else if (data instanceof Blob) {
            const arrayBuffer = await data.arrayBuffer();
            uint8Array = new Uint8Array(arrayBuffer);
        } else if (typeof data === 'string') {
            // Assume base64 encoded string
            const binaryString = atob(data);
            uint8Array = new Uint8Array(binaryString.length);
            for (let i = 0; i < binaryString.length; i++) {
                uint8Array[i] = binaryString.charCodeAt(i);
            }
        } else {
            throw new Error('Unsupported data type for saveBinaryFile');
        }

        await saveFile(filepath, uint8Array);
    };

    // Helper to fetch and save a remote file (e.g., fonts, images)
    const fetchAndSaveFile = async (filepath, url) => {
        const response = await fetch(url);
        if (!response.ok) {
            throw new Error(`Failed to fetch ${url}: ${response.statusText}`);
        }
        const data = await response.arrayBuffer();
        await saveBinaryFile(filepath, data);
        if (verbose) {
            console.log(`[CircuitPython] Fetched and saved ${filepath} (${data.byteLength} bytes)`);
        }
    };

    // Auto-run boot.py and code.py if requested
    if (autoRun) {
        if (verbose) {
            console.log('[CircuitPython] Auto-running boot/code workflow');
        }
        try {
            runWorkflow();
        } catch (error) {
            if (verbose) {
                console.error('[CircuitPython] Auto-run failed:', error);
            }
            // Don't throw - let the runtime continue
        }
    }

    return {
        _module: Module,
        filesystem: persistentFS,
        PyProxy: PyProxy,
        FS: Module.FS,
        runFile: runFile,
        runWorkflow: runWorkflow,
        saveFile: saveFile,
        saveBinaryFile: saveBinaryFile,
        fetchAndSaveFile: fetchAndSaveFile,
        globals: {
            __dict__: pyimport("__main__").__dict__,
            get(key) {
                return this.__dict__[key];
            },
            set(key, value) {
                this.__dict__[key] = value;
            },
            delete(key) {
                delete this.__dict__[key];
            },
        },
        registerJsModule(name, module) {
            const value = Module._malloc(3 * 4);
            proxy_convert_js_to_mp_obj_jsside(module, value);
            Module.ccall(
                "mp_js_register_js_module",
                "null",
                ["string", "pointer"],
                [name, value],
            );
            Module._free(value);
        },
        pyimport: pyimport,
        runPython(code) {
            const len = Module.lengthBytesUTF8(code);
            const buf = Module._malloc(len + 1);
            Module.stringToUTF8(code, buf, len + 1);
            const value = Module._malloc(3 * 4);
            Module.ccall(
                "mp_js_do_exec",
                "number",
                ["pointer", "number", "pointer"],
                [buf, len, value],
            );
            Module._free(buf);
            return proxy_convert_mp_to_js_obj_jsside_with_free(value);
        },
        async runPythonAsync(code) {
            const len = Module.lengthBytesUTF8(code);
            const buf = Module._malloc(len + 1);
            Module.stringToUTF8(code, buf, len + 1);
            const value = Module._malloc(3 * 4);
            // CIRCUITPY-CHANGE: Mark as async for ASYNCIFY variant
            await Module.ccall(
                "mp_js_do_exec_async",
                "number",
                ["pointer", "number", "pointer"],
                [buf, len, value],
                { async: true }
            );
            Module._free(buf);
            const ret = proxy_convert_mp_to_js_obj_jsside_with_free(value);
            if (ret instanceof PyProxyThenable) {
                return Promise.resolve(ret);
            }
            return ret;
        },
        replInit() {
            Module.ccall("mp_js_repl_init", "null", ["null"]);
        },
        replProcessChar(chr) {
            return Module.ccall(
                "mp_js_repl_process_char",
                "number",
                ["number"],
                [chr],
            );
        },
        // Needed if the GC/asyncify is enabled.
        async replProcessCharWithAsyncify(chr) {
            return Module.ccall(
                "mp_js_repl_process_char",
                "number",
                ["number"],
                [chr],
                { async: true },
            );
        },
        // CIRCUITPY-CHANGE: String-based REPL helpers for easier xterm.js integration
        serial: {
            // Write a string to the REPL input buffer
            writeInput(text) {
                const len = Module.lengthBytesUTF8(text);
                const buf = Module._malloc(len + 1);
                Module.stringToUTF8(text, buf, len + 1);
                const written = Module.ccall(
                    "board_serial_write_input",
                    "number",
                    ["pointer", "number"],
                    [buf, len]
                );
                Module._free(buf);
                return written;
            },
            // Write a single character to the REPL input buffer
            writeChar(char) {
                const charCode = typeof char === 'string' ? char.charCodeAt(0) : char;
                return Module.ccall(
                    "board_serial_write_input_char",
                    "number",
                    ["number"],
                    [charCode]
                );
            },
            // Clear the input buffer
            clearInput() {
                Module.ccall("board_serial_clear_input", "null", []);
            },
            // Get number of bytes available in input buffer
            inputAvailable() {
                return Module.ccall("board_serial_input_available", "number", []);
            },
            // Process a string through the REPL (writes to buffer)
            processString(text) {
                const len = Module.lengthBytesUTF8(text);
                const buf = Module._malloc(len + 1);
                Module.stringToUTF8(text, buf, len + 1);
                const result = Module.ccall(
                    "board_serial_repl_process_string",
                    "number",
                    ["pointer", "number"],
                    [buf, len]
                );
                Module._free(buf);
                return result;
            },
            // Set output callback for REPL output (easier than hooking stdout)
            onOutput(callback) {
                // Store callback in Module for C code to call
                Module._serialOutputCallback = callback;

                // Create a wrapper function that C can call
                const wrapperPtr = Module.addFunction((textPtr, length) => {
                    const text = Module.UTF8ToString(textPtr, length);
                    callback(text);
                }, 'vii');

                Module.ccall(
                    "board_serial_set_output_callback",
                    "null",
                    ["number"],
                    [wrapperPtr]
                );
            }
        },
        // CIRCUITPY-CHANGE: Virtual hardware interface for input simulation and output observation
        // Allows JavaScript to interact with the WASM runtime like the physical world interacts with a board
        _virtual_gpio_set_input_value: Module._virtual_gpio_set_input_value,
        _virtual_gpio_get_output_value: Module._virtual_gpio_get_output_value,
        _virtual_gpio_get_direction: Module._virtual_gpio_get_direction,
        _virtual_gpio_get_pull: Module._virtual_gpio_get_pull,
        _virtual_analog_set_input_value: Module._virtual_analog_set_input_value,
        _virtual_analog_get_output_value: Module._virtual_analog_get_output_value,
        _virtual_analog_is_enabled: Module._virtual_analog_is_enabled,
        _virtual_analog_is_output: Module._virtual_analog_is_output,
        // CIRCUITPY-CHANGE: Code analysis functions for cooperative yielding supervisor
        _analyze_code_structure: Module._analyze_code_structure,
        _is_valid_python_syntax: Module._is_valid_python_syntax,
        _extract_loop_body: Module._extract_loop_body,
        // High-level JavaScript-friendly wrappers for code analysis
        analyzeCode(code) {
            const len = Module.lengthBytesUTF8(code);
            const codePtr = Module._malloc(len + 1);
            Module.stringToUTF8(code, codePtr, len + 1);

            const resultPtr = Module._analyze_code_structure(codePtr, len);
            Module._free(codePtr);

            if (!resultPtr) {
                return null;
            }

            // Read code_structure_t from memory
            // Note: resultPtr points to a static struct in C, not heap-allocated, so don't free it

            // Memory layout (32-bit WASM):
            // loop_info_t loops[16] - 16 loops * 16 bytes each = 256 bytes (offset 0)
            //   Each loop_info_t: loop_type(4), line(4), column(4), needs_instrumentation(1), padding(3)
            // int loop_count - 4 bytes (offset 256)
            // Legacy fields:
            //   bool has_while_true_loop - 1 byte (offset 260)
            //   padding - 3 bytes
            //   size_t while_true_line - 4 bytes (offset 264)
            //   size_t while_true_column - 4 bytes (offset 268)
            //   bool has_async_def - 1 byte (offset 272)
            //   bool has_await - 1 byte (offset 273)
            //   bool has_asyncio_run - 1 byte (offset 274)
            //   padding - 1 byte
            //   int token_count - 4 bytes (offset 276)

            const loopCount = Module.HEAP32[(resultPtr + 256) / 4];
            const loops = [];

            // Loop type enum values (must match C enum)
            const LoopType = {
                WHILE_TRUE: 0,
                WHILE_NUMERIC: 1,
                WHILE_GENERAL: 2,
                FOR_GENERAL: 3,
                FOR_RANGE: 4
            };

            // Read each loop from the loops array
            for (let i = 0; i < loopCount && i < 16; i++) {
                const loopOffset = resultPtr + (i * 16);  // Each loop_info_t is 16 bytes
                loops.push({
                    loopType: Module.HEAP32[loopOffset / 4],           // loop_type (enum as int)
                    line: Module.HEAPU32[(loopOffset + 4) / 4],        // line
                    column: Module.HEAPU32[(loopOffset + 8) / 4],      // column
                    needsInstrumentation: !!Module.HEAPU8[loopOffset + 12]  // needs_instrumentation
                });
            }

            const result = {
                loops,
                loopCount,
                LoopType,  // Export enum for users of this API
                // Legacy fields for backward compatibility
                hasWhileTrueLoop: !!Module.HEAPU8[resultPtr + 260],
                whileTrueLine: Module.HEAPU32[(resultPtr + 264) / 4],
                whileTrueColumn: Module.HEAPU32[(resultPtr + 268) / 4],
                hasAsyncDef: !!Module.HEAPU8[resultPtr + 272],
                hasAwait: !!Module.HEAPU8[resultPtr + 273],
                hasAsyncioRun: !!Module.HEAPU8[resultPtr + 274],
                tokenCount: Module.HEAP32[(resultPtr + 276) / 4]
            };

            return result;
        },
        isValidPythonSyntax(code) {
            const len = Module.lengthBytesUTF8(code);
            const codePtr = Module._malloc(len + 1);
            Module.stringToUTF8(code, codePtr, len + 1);

            const result = Module._is_valid_python_syntax(codePtr, len);
            Module._free(codePtr);

            return !!result;
        },
    };
}

globalThis.loadCircuitPython = loadCircuitPython;

// Export worker-based loader for browser environments
// This runs CircuitPython in a Web Worker to prevent blocking the main thread
// Worker export disabled for command-line testing
// export { loadCircuitPythonWorker } from './circuitpython-worker-proxy.js';

async function runCLI() {
    const fs = await import("fs");
    let heap_size = 128 * 1024;
    let contents = "";
    let repl = true;

    for (let i = 2; i < process.argv.length; i++) {
        if (process.argv[i] === "-X" && i < process.argv.length - 1) {
            if (process.argv[i + 1].includes("heapsize=")) {
                heap_size = parseInt(process.argv[i + 1].split("heapsize=")[1]);
                const suffix = process.argv[i + 1].substr(-1).toLowerCase();
                if (suffix === "k") {
                    heap_size *= 1024;
                } else if (suffix === "m") {
                    heap_size *= 1024 * 1024;
                }
                ++i;
            }
        } else {
            contents += fs.readFileSync(process.argv[i], "utf8");
            repl = false;
        }
    }

    if (process.stdin.isTTY === false) {
        contents = fs.readFileSync(0, "utf8");
        repl = false;
    }

    const ctpy = await loadCircuitPython({
        heapsize: heap_size,
        stdout: (data) => process.stdout.write(data),
        linebuffer: false,
    });

    if (repl) {
        ctpy.replInit();
        if (process.stdin.setRawMode) {
            process.stdin.setRawMode(true);
        }

        // Process input character by character through the REPL
        // The event-driven REPL expects characters to be pushed to it
        process.stdin.on("data", (data) => {
            const text = data.toString('utf8');

            // Check for Ctrl+D to exit
            for (let i = 0; i < text.length; i++) {
                const charCode = text.charCodeAt(i);
                if (charCode === 0x04) {  // Ctrl+D
                    process.stdout.write('\r\n');  // Write newline before exit
                    process.exit();
                }
                // Pass each character to the REPL
                const result = ctpy.replProcessChar(charCode);
                if (result !== 0) {
                    process.exit();
                }
            }
        });
    } else{
        // If the script to run ends with a running of the asyncio main loop, then inject
        // a simple `asyncio.run` hook that starts the main task.  This is primarily to
        // support running the standard asyncio tests.
        if (contents.endsWith("asyncio.run(main())\n")) {
            const asyncio = ctpy.pyimport("asyncio");
            asyncio.run = async (task) => {
                await asyncio.create_task(task);
            };
        }

        try {
            ctpy.runPython(contents);
        } catch (error) {
            if (error.name === "PythonError") {
                if (error.type === "SystemExit") {
                    // SystemExit, this is a valid exception to successfully end a script.
                } else {
                    // An unhandled Python exception, print in out.
                    console.error(error.message);
                }
            } else {
                // A non-Python exception.  Re-raise it.
                throw error;
            }
        }
    }
}

// Check if Node is running (equivalent to ENVIRONMENT_IS_NODE).
if (
    typeof process === "object" &&
    typeof process.versions === "object" &&
    typeof process.versions.node === "string"
) {
    // Check if this module is run from the command line via `node micropython.mjs`.
    //
    // See https://stackoverflow.com/questions/6398196/detect-if-called-through-require-or-directly-by-command-line/66309132#66309132
    //
    // Note:
    // - `resolve()` is used to handle symlinks
    // - `includes()` is used to handle cases where the file extension was omitted when passed to node

    if (process.argv.length > 1) {
        const path = await import("path");
        const url = await import("url");

        const pathToThisFile = path.resolve(url.fileURLToPath(import.meta.url));
        const pathPassedToNode = path.resolve(process.argv[1]);
        const isThisFileBeingRunViaCLI =
            pathToThisFile.includes(pathPassedToNode);

        if (isThisFileBeingRunViaCLI) {
            runCLI();
        }
    }
}
