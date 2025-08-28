/*
 * CircuitPython WebAssembly JavaScript API
 * Node.js-focused interface with Web Worker compatibility
 * 
 * Enhanced from MicroPython WebAssembly API with CircuitPython-specific features:
 * - Node.js-first architecture with Web Worker fallback
 * - Hardware simulation capabilities  
 * - Enhanced development tools and debugging
 * - CircuitPython library compatibility layer
 */

// Runtime detection
const isNode = typeof process !== 'undefined' && process.versions && process.versions.node;
const isWebWorker = typeof importScripts !== 'undefined';

// Enhanced error handling with CircuitPython context
class PythonError extends Error {
    constructor(message, type = 'PythonError', traceback = null) {
        super(message);
        this.name = 'PythonError';
        this.type = type;
        this.traceback = traceback;
        
        // CircuitPython-specific error context
        this.isCircuitPythonError = true;
        this.runtime = isNode ? 'nodejs' : (isWebWorker ? 'webworker' : 'browser');
    }
}

// Proxy system integration (based on MicroPython's approach)
let Module = null;

// Proxy conversion functions - will be populated after module loading
let proxy_convert_mp_to_js_obj_jsside_with_free = null;
let proxy_convert_js_to_mp_obj_jsside = null;
let proxy_js_init = null;

// Convert CircuitPython result using MicroPython proxy constants  
function convertCircuitPythonResult(module, objPtr) {
    try {
        const type = module.getValue(objPtr, 'i32');
        
        switch (type) {
            case 0: // PROXY_KIND_MP_NULL  
                return null;
                
            case 1: // PROXY_KIND_MP_NONE
                return null;
                
            case 2: // PROXY_KIND_MP_BOOL
                return Boolean(module.getValue(objPtr + 4, 'i32'));
                
            case 3: // PROXY_KIND_MP_INT
                return module.getValue(objPtr + 4, 'i32');
                
            case 4: // PROXY_KIND_MP_FLOAT
                const floatValue = module.getValue(objPtr + 4, 'double');
                // Check for problematic float values
                if (!Number.isFinite(floatValue)) {
                    console.warn('Non-finite float value detected:', floatValue);
                    return null;
                }
                return floatValue;
                
            case 5: // PROXY_KIND_MP_STR
                const strPtr = module.getValue(objPtr + 4, '*');
                const strLen = module.getValue(objPtr + 8, 'i32');
                
                if (strPtr === 0) {
                    return "";
                }
                
                // Use Emscripten UTF8ToString
                if (module.UTF8ToString && strLen > 0) {
                    return module.UTF8ToString(strPtr, strLen);
                } else if (module.UTF8ToString) {
                    return module.UTF8ToString(strPtr);
                } else {
                    return `<string at 0x${strPtr.toString(16)}>`;
                }
                
            case -1: // PROXY_KIND_MP_EXCEPTION
                const excPtr = module.getValue(objPtr + 4, '*');
                const excLen = module.getValue(objPtr + 8, 'i32');
                
                let excMsg = "Python Exception";
                if (excPtr !== 0 && module.UTF8ToString) {
                    if (excLen > 0) {
                        excMsg = module.UTF8ToString(excPtr, excLen);
                    } else {
                        excMsg = module.UTF8ToString(excPtr);
                    }
                }
                throw new PythonError(excMsg);
                
            default:
                return `<unknown object type ${type}>`;
        }
    } catch (error) {
        if (error instanceof PythonError) {
            throw error;
        }
        throw new PythonError(`Result conversion failed: ${error.message}`);
    }
}

// Enhanced options for CircuitPython WebAssembly (Node.js-focused)
// - pystack: Python call stack size in words (default: adaptive based on runtime)
// - heapsize: GC heap size in bytes (default: adaptive based on runtime)  
// - stdout/stderr: output handlers (default: adaptive based on runtime)
// - url: location to load circuitpython.mjs from
// - enableHardwareSimulation: mock hardware APIs for testing (Node.js only)
// - enableDevelopmentMode: enhanced debugging and development features
// - circuitpythonLibPath: path to CircuitPython libraries for Node.js
export async function loadCircuitPython(options = {}) {
    // Adaptive defaults based on runtime environment
    const runtimeDefaults = isNode ? {
        pystack: 16384,           // 16KB Python stack for Node.js
        heapsize: 2 * 1024 * 1024, // 2MB heap for Node.js
        enableHardwareSimulation: true,
        enableDevelopmentMode: true
    } : isWebWorker ? {
        pystack: 12288,           // 12KB Python stack for Web Worker  
        heapsize: 1024 * 1024,    // 1MB heap for Web Worker
        enableHardwareSimulation: false,
        enableDevelopmentMode: false
    } : {
        pystack: 8192,            // 8KB Python stack for browser
        heapsize: 512 * 1024,     // 512KB heap for browser
        enableHardwareSimulation: false,
        enableDevelopmentMode: false
    };

    const {
        pystack = runtimeDefaults.pystack,
        heapsize = runtimeDefaults.heapsize,
        stdout = isNode ? ((line) => process.stdout.write(line + '\n')) : console.log,
        stderr = isNode ? ((line) => process.stderr.write(line + '\n')) : console.error,
        url,
        enableHardwareSimulation = runtimeDefaults.enableHardwareSimulation,
        enableDevelopmentMode = runtimeDefaults.enableDevelopmentMode,
        circuitpythonLibPath = isNode ? './circuitpython_libs' : null,
        linebuffer = true
    } = Object.assign(runtimeDefaults, options);

    // Configure module loading (MicroPython-compatible)
    let Module = {};
    Module.locateFile = (path, scriptDirectory) => url || scriptDirectory + path;
    Module._textDecoder = new TextDecoder();
    
    // Enhanced I/O handling with runtime-specific optimizations
    if (stdout !== undefined) {
        if (linebuffer) {
            Module._stdoutBuffer = [];
            Module.stdout = (c) => {
                if (c === 10) {
                    stdout(Module._textDecoder.decode(new Uint8Array(Module._stdoutBuffer)));
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
                    stderr(Module._textDecoder.decode(new Uint8Array(Module._stderrBuffer)));
                    Module._stderrBuffer = [];
                } else {
                    Module._stderrBuffer.push(c);
                }
            };
        } else {
            Module.stderr = (c) => stderr(new Uint8Array([c]));
        }
    }

    // Load CircuitPython WebAssembly module - adjust path as needed
    let loadCircuitPythonWasm;
    try {
        loadCircuitPythonWasm = (await import('./build-standard/circuitpython.mjs')).default;
    } catch (err) {
        // Fallback path
        try {
            loadCircuitPythonWasm = (await import('./circuitpython.mjs')).default;
        } catch (err2) {
            throw new Error(`Failed to load CircuitPython module: ${err.message}. Also tried: ${err2.message}`);
        }
    }
    Module = await loadCircuitPythonWasm(Module);
    
    // Set global Module reference for proxy system
    globalThis.Module = Module;
    
    // Initialize proxy system (if available)
    if (typeof proxy_js_init === 'function') {
        proxy_js_init();
    }
    
    // Initialize CircuitPython with MicroPython-compatible API
    Module.ccall('mp_js_init', 'null', ['number', 'number'], [pystack, heapsize]);
    if (Module.ccall && typeof Module.ccall('proxy_c_init', 'null', [], []) !== 'undefined') {
        // Proxy system is available
    }
    
    // Hardware simulation setup (Node.js only)
    let hardwareSimulator = null;
    if (enableHardwareSimulation && isNode) {
        hardwareSimulator = await setupHardwareSimulation();
    }
    
    // Enhanced API with MicroPython compatibility + CircuitPython features
    const pyimport = (name) => {
        const value = Module._malloc(3 * 4);
        Module.ccall('mp_js_do_import', 'null', ['string', 'pointer'], [name, value]);
        
        // Use proxy system if available, fallback to basic conversion
        if (proxy_convert_mp_to_js_obj_jsside_with_free) {
            return proxy_convert_mp_to_js_obj_jsside_with_free(value);
        } else {
            // Fallback for basic object conversion
            const result = convertCircuitPythonResult(Module, value);
            Module._free(value);
            return result;
        }
    };

    const api = {
        _module: Module,
        
        // Core MicroPython-compatible API
        PyProxy: typeof PyProxy !== 'undefined' ? PyProxy : null,
        FS: Module.FS,
        
        // Global namespace access (MicroPython-compatible)
        globals: {
            __dict__: pyimport('__main__').__dict__ || {},
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
        
        // Module registration (MicroPython-compatible)
        registerJsModule(name, module) {
            const value = Module._malloc(3 * 4);
            if (proxy_convert_js_to_mp_obj_jsside) {
                proxy_convert_js_to_mp_obj_jsside(module, value);
                Module.ccall('mp_js_register_js_module', 'null', ['string', 'pointer'], [name, value]);
                Module._free(value);
            } else {
                throw new Error('Proxy system not available for module registration');
            }
        },
        
        // Python import (MicroPython-compatible)
        pyimport: pyimport,
        
        // Execute Python code (MicroPython-compatible)
        runPython(code, options = {}) {
            const len = code.length;
            const buf = Module._malloc(len + 1);
            Module.stringToUTF8(code, buf, len + 1);
            
            if (options.skipResultConversion) {
                // Simple execution without result conversion (like test-simple.js)
                const value = Module._malloc(3 * 4);
                try {
                    Module.ccall('mp_js_do_exec', 'null', ['pointer', 'number', 'pointer'], [buf, len, value]);
                    Module._free(buf);
                    Module._free(value);
                    return undefined; // No result conversion
                } catch (error) {
                    Module._free(buf);
                    Module._free(value);
                    throw error;
                }
            } else {
                // Full execution with result conversion
                const value = Module._malloc(3 * 4);
                try {
                    Module.ccall('mp_js_do_exec', 'null', ['pointer', 'number', 'pointer'], [buf, len, value]);
                    Module._free(buf);
                    
                    if (proxy_convert_mp_to_js_obj_jsside_with_free) {
                        return proxy_convert_mp_to_js_obj_jsside_with_free(value);
                    } else {
                        const result = convertCircuitPythonResult(Module, value);
                        Module._free(value);
                        return result;
                    }
                } catch (error) {
                    Module._free(buf);
                    Module._free(value);
                    throw error;
                }
            }
        },
        
        // Simple execution without result conversion (safe mode)
        runPythonSimple(code) {
            return this.runPython(code, { skipResultConversion: true });
        },
        
        // Async Python execution (MicroPython-compatible)
        runPythonAsync(code) {
            const len = code.length;
            const buf = Module._malloc(len + 1);
            Module.stringToUTF8(code, buf, len + 1);
            const value = Module._malloc(3 * 4);
            Module.ccall('mp_js_do_exec_async', 'null', ['pointer', 'number', 'pointer'], [buf, len, value]);
            Module._free(buf);
            
            let ret;
            if (proxy_convert_mp_to_js_obj_jsside_with_free) {
                ret = proxy_convert_mp_to_js_obj_jsside_with_free(value);
            } else {
                ret = convertCircuitPythonResult(Module, value);
                Module._free(value);
            }
            
            // Handle async results
            if (ret && typeof ret.then === 'function') {
                return Promise.resolve(ret);
            }
            return ret;
        },
        
        // REPL functions (MicroPython-compatible)
        replInit() {
            Module.ccall('mp_js_repl_init', 'null', []);
        },
        
        replProcessChar(chr) {
            return Module.ccall('mp_js_repl_process_char', 'number', ['number'], [chr]);
        },
        
        // Enhanced REPL for async support
        async replProcessCharWithAsyncify(chr) {
            return Module.ccall('mp_js_repl_process_char', 'number', ['number'], [chr], { async: true });
        },
        
        // CircuitPython-specific enhancements
        get runtime() {
            return {
                type: isNode ? 'nodejs' : (isWebWorker ? 'webworker' : 'browser'),
                version: isNode ? process.versions.node : null,
                isNode,
                isWebWorker,
                enabledFeatures: {
                    hardwareSimulation: enableHardwareSimulation,
                    developmentMode: enableDevelopmentMode,
                    proxySystem: !!proxy_convert_mp_to_js_obj_jsside_with_free
                }
            };
        },
        
        // Hardware simulation (CircuitPython-specific, Node.js only)
        get hardware() {
            return hardwareSimulator || null;
        },
        
        // Development tools (CircuitPython-specific)
        get dev() {
            return enableDevelopmentMode ? {
                debugInfo: () => ({
                    heapSize: heapsize,
                    pyStackSize: pystack,
                    moduleInfo: Module,
                    memoryUsage: isNode ? process.memoryUsage() : null
                }),
                
                // Enhanced error reporting
                enableVerboseErrors: () => {
                    // Implementation for enhanced error reporting
                },
                
                // Performance profiling
                profile: (func) => {
                    const start = performance.now();
                    const result = func();
                    const end = performance.now();
                    return { result, executionTime: end - start };
                }
            } : null;
        }
    };
    
    return api;
}

// Hardware simulation setup (CircuitPython-specific, Node.js only)
async function setupHardwareSimulation() {
    if (!isNode) return null;
    
    // Mock CircuitPython hardware modules
    return {
        // Mock digital I/O
        digitalio: {
            DigitalInOut: class DigitalInOut {
                constructor(pin) {
                    this.pin = pin;
                    this.value = false;
                }
                
                switch_to_output() {
                    console.log(`[HW SIM] Pin ${this.pin} set to output`);
                }
                
                switch_to_input() {
                    console.log(`[HW SIM] Pin ${this.pin} set to input`);
                }
                
                get value() { return this._value || false; }
                set value(val) {
                    this._value = val;
                    console.log(`[HW SIM] Pin ${this.pin} = ${val}`);
                }
            }
        },
        
        // Mock board pins
        board: new Proxy({}, {
            get(target, prop) {
                return `${prop}_PIN`;
            }
        }),
        
        // Mock time functions  
        time: {
            sleep: (seconds) => new Promise(resolve => setTimeout(resolve, seconds * 1000))
        }
    };
}

// Enhanced Node.js CLI with CircuitPython-specific features
async function runCLI() {
    if (!isNode) return;
    
    const fs = await import('fs');
    const path = await import('path');
    
    // Enhanced argument parsing for CircuitPython
    let heap_size = 2 * 1024 * 1024; // 2MB default for Node.js
    let contents = '';
    let repl = true;
    let enableHardwareSim = true;
    let enableDevMode = true;
    let circuitpythonLibPath = './circuitpython_libs';
    
    for (let i = 2; i < process.argv.length; i++) {
        const arg = process.argv[i];
        
        if (arg === '-X' && i < process.argv.length - 1) {
            const param = process.argv[i + 1];
            if (param.includes('heapsize=')) {
                heap_size = parseInt(param.split('heapsize=')[1]);
                const suffix = param.slice(-1).toLowerCase();
                if (suffix === 'k') heap_size *= 1024;
                else if (suffix === 'm') heap_size *= 1024 * 1024;
                ++i;
            }
        } else if (arg === '--no-hardware-sim') {
            enableHardwareSim = false;
        } else if (arg === '--no-dev-mode') {
            enableDevMode = false;
        } else if (arg === '--circuitpython-libs' && i < process.argv.length - 1) {
            circuitpythonLibPath = process.argv[i + 1];
            ++i;
        } else if (!arg.startsWith('-')) {
            contents += fs.readFileSync(arg, 'utf8');
            repl = false;
        }
    }
    
    // Check for piped input
    if (process.stdin.isTTY === false) {
        contents = fs.readFileSync(0, 'utf8');
        repl = false;
    }
    
    // Load CircuitPython with Node.js optimizations
    const cp = await loadCircuitPython({
        heapsize: heap_size,
        stdout: (data) => process.stdout.write(data),
        stderr: (data) => process.stderr.write(data),
        linebuffer: false,
        enableHardwareSimulation: enableHardwareSim,
        enableDevelopmentMode: enableDevMode,
        circuitpythonLibPath
    });
    
    // Register hardware simulation modules if enabled
    if (enableHardwareSim && cp.hardware) {
        try {
            cp.registerJsModule('digitalio', cp.hardware.digitalio);
            cp.registerJsModule('board', cp.hardware.board);
            cp.registerJsModule('time', cp.hardware.time);
            console.log('[CircuitPython] Hardware simulation enabled');
        } catch (err) {
            console.warn('[CircuitPython] Hardware simulation setup failed:', err.message);
        }
    }
    
    if (repl) {
        // Enhanced REPL mode with CircuitPython banner
        console.log(`CircuitPython ${cp.runtime.type} REPL`);
        console.log(`Runtime: Node.js ${cp.runtime.version}`);
        if (enableHardwareSim) console.log('Hardware simulation: enabled');
        console.log('Use Ctrl+C to exit\n');
        
        cp.replInit();
        process.stdin.setRawMode(true);
        process.stdin.on('data', async (data) => {
            for (let i = 0; i < data.length; i++) {
                const result = await cp.replProcessCharWithAsyncify(data[i]);
                if (result) {
                    process.exit();
                }
            }
        });
    } else {
        // Script execution mode with enhanced error handling
        try {
            // Handle CircuitPython async patterns
            if (contents.includes('asyncio') && (contents.includes('asyncio.run') || contents.includes('await '))) {
                const result = await cp.runPythonAsync(contents);
                if (result && typeof result.then === 'function') {
                    await result;
                }
            } else {
                cp.runPython(contents);
            }
        } catch (error) {
            if (error.isCircuitPythonError) {
                if (error.type === 'SystemExit') {
                    // Normal exit
                } else {
                    console.error(`CircuitPython Error (${error.type}): ${error.message}`);
                    if (enableDevMode && error.traceback) {
                        console.error('Traceback:', error.traceback);
                    }
                    process.exit(1);
                }
            } else {
                throw error;
            }
        }
    }
}

// Make available globally and as default export
globalThis.loadCircuitPython = loadCircuitPython;
export default loadCircuitPython;

// Auto-run CLI in Node.js when executed directly
if (isNode && process.argv.length > 1) {
    const path = await import('path');
    const url = await import('url');
    
    const pathToThisFile = path.resolve(url.fileURLToPath(import.meta.url));
    const pathPassedToNode = path.resolve(process.argv[1]);
    const isThisFileBeingRunViaCLI = pathToThisFile.includes(pathPassedToNode);
    
    if (isThisFileBeingRunViaCLI) {
        runCLI();
    }
}