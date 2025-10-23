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
// - verbose: whether to log infrastructure messages (VirtualClock, init). Default: false
// - autoRun: whether to automatically run boot.py and code.py on load. Default: false
// CIRCUITPY_CHANGE: export function name change
// - virtual clock: initialize virtual clock for timing control
export async function loadCircuitPython(options) {
    const { pystack, heapsize, url, stdin, stdout, stderr, linebuffer, verbose, autoRun } =
        Object.assign(
            { pystack: 2 * 1024, heapsize: 1024 * 1024, linebuffer: true, verbose: false, autoRun: false },
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
    Module.ccall(
        "mp_js_init",
        "null",
        ["number", "number"],
        [pystack, heapsize],
    );
    Module.ccall("proxy_c_init", "null", [], []);

    // CIRCUITPY-CHANGE: Initialize persistent filesystem if requested
    let persistentFS = null;
    if (filesystem === 'indexeddb') {
        try {
            // Import filesystem module dynamically
            const { CircuitPythonFilesystem } = await import('./filesystem.js');
            persistentFS = new CircuitPythonFilesystem(verbose);
            await persistentFS.init();

            // Sync files from IndexedDB to VFS before running any code
            await persistentFS.syncToVFS(Module);

            if (verbose) {
                console.log('[CircuitPython] Persistent filesystem initialized');
            }
        } catch (e) {
            if (verbose) {
                console.warn('[CircuitPython] Persistent filesystem initialization failed:', e);
            }
        }
    }

    // CIRCUITPY-CHANGE: Initialize virtual clock for timing control
    let virtualClock = null;
    try {
        // Get pointer to virtual hardware
        const virtualHardwarePtr = Module._get_virtual_hardware_ptr();
        if (virtualHardwarePtr) {
            // Create a simple object that looks like WASM instance for VirtualClock
            const wasmInstance = {
                exports: {
                    get_virtual_hardware_ptr: () => virtualHardwarePtr
                }
            };
            const wasmMemory = {
                buffer: Module.HEAPU8.buffer
            };
            virtualClock = new VirtualClock(wasmInstance, wasmMemory, verbose);
            // Start in realtime mode by default
            virtualClock.startRealtime();
            if (verbose) {
                console.log('[CircuitPython] Virtual clock initialized in REALTIME mode');
            }
        }
    } catch (e) {
        if (verbose) {
            console.warn('[CircuitPython] Virtual clock initialization failed:', e);
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
        virtualClock: virtualClock,
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
        runPythonAsync(code) {
            const len = Module.lengthBytesUTF8(code);
            const buf = Module._malloc(len + 1);
            Module.stringToUTF8(code, buf, len + 1);
            const value = Module._malloc(3 * 4);
            Module.ccall(
                "mp_js_do_exec_async",
                "number",
                ["pointer", "number", "pointer"],
                [buf, len, value],
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
    };
}

globalThis.loadCircuitPython = loadCircuitPython;

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
        process.stdin.setRawMode(true);
        process.stdin.on("data", (data) => {
            for (let i = 0; i < data.length; i++) {
                ctpy.replProcessCharWithAsyncify(data[i]).then((result) => {
                    if (result) {
                        process.exit();
                    }
                });
            }
        });
    } else {
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
