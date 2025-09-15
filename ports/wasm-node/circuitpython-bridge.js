/**
 * CircuitPython WebAssembly Bridge
 * Modern JavaScript interface for CircuitPython WASM with proper async support
 */

export class CircuitPythonBridge {
    constructor() {
        this.module = null;
        this.initialized = false;
        this.outputBuffer = [];
        this.errorBuffer = [];
        
        // SharedArrayBuffer for efficient communication (if available)
        if (typeof SharedArrayBuffer !== 'undefined') {
            this.sharedMemory = new SharedArrayBuffer(1024 * 1024);
            this.inputBuffer = new Uint8Array(this.sharedMemory, 0, 4096);
            this.outputSharedBuffer = new Uint8Array(this.sharedMemory, 4096, 4096);
        }
    }
    
    /**
     * Initialize CircuitPython WebAssembly module with improved error handling
     */
    async init(options = {}) {
        const defaultOptions = {
            heapSize: 8 * 1024 * 1024,  // 8MB heap
            stackSize: 256 * 1024,        // 256KB stack
            print: (text) => {
                this.outputBuffer.push(text);
                if (options.onOutput) {
                    options.onOutput(text);
                }
            },
            printErr: (text) => {
                this.errorBuffer.push(text);
                if (options.onError) {
                    options.onError(text);
                }
            }
        };
        
        const mergedOptions = { ...defaultOptions, ...options };
        
        try {
            // Import the CircuitPython module
            const CircuitPython = await import('./build-standard/circuitpython.mjs');
            this.module = await CircuitPython.default(mergedOptions);
            
            // Initialize the interpreter with deferred initialization
            this.module._mp_js_init_with_heap(mergedOptions.heapSize);
            
            // Wait briefly for initialization to complete
            await this.waitForInitialization(500); // 500ms timeout
            
            // Post-initialization is now handled automatically in _mp_js_init_with_heap
            // but we can call it explicitly for fine-grained control if needed
            if (this.module._mp_js_post_init) {
                this.module._mp_js_post_init();
            }
            
            // Wait for proxy system to be ready before REPL init
            await this.waitForProxy(200); // 200ms timeout
            
            // Initialize REPL after proxy system is ready
            this.module._mp_js_repl_init();
            
            this.initialized = true;
            return this;
            
        } catch (error) {
            console.error('CircuitPython initialization failed:', error);
            throw new Error(`Failed to initialize CircuitPython: ${error.message}`);
        }
    }
    
    /**
     * Wait for basic initialization to complete
     */
    async waitForInitialization(timeout = 500) {
        const start = Date.now();
        while (Date.now() - start < timeout) {
            try {
                if (this.module && this.module._mp_js_init_with_heap) {
                    return; // Basic initialization appears successful
                }
            } catch (e) {
                // Continue waiting
            }
            await new Promise(resolve => setTimeout(resolve, 10));
        }
        throw new Error('Initialization timeout');
    }
    
    /**
     * Wait for proxy system to be ready
     */
    async waitForProxy(timeout = 200) {
        const start = Date.now();
        while (Date.now() - start < timeout) {
            try {
                // Test if proxy system is responding
                if (this.module._proxy_c_is_initialized && this.module._proxy_c_is_initialized()) {
                    return; // Proxy system is ready
                }
            } catch (e) {
                // Continue waiting
            }
            await new Promise(resolve => setTimeout(resolve, 10));
        }
        // Don't throw error - proxy might not be needed for basic functionality
        console.warn('Proxy system not ready within timeout, continuing anyway');
    }
    
    /**
     * Execute Python code asynchronously
     */
    async execute(code) {
        if (!this.initialized) {
            throw new Error('CircuitPython not initialized. Call init() first.');
        }
        
        return new Promise((resolve, reject) => {
            try {
                const out = new Uint32Array(3);
                const codePtr = this.module.allocateUTF8(code);
                
                this.module._mp_js_do_exec(codePtr, code.length, out.byteOffset);
                this.module._free(codePtr);
                
                // Check for exceptions
                if (out[0] === 0) {
                    // Success
                    resolve({
                        success: true,
                        output: this.outputBuffer.join('\n'),
                        result: out[1]
                    });
                } else {
                    // Error
                    reject({
                        success: false,
                        error: this.errorBuffer.join('\n'),
                        exception: out[1]
                    });
                }
                
                // Clear buffers
                this.outputBuffer = [];
                this.errorBuffer = [];
            } catch (error) {
                reject({
                    success: false,
                    error: error.message
                });
            }
        });
    }
    
    /**
     * Import a Python module
     */
    async importModule(moduleName) {
        if (!this.initialized) {
            throw new Error('CircuitPython not initialized. Call init() first.');
        }
        
        return new Promise((resolve, reject) => {
            try {
                const out = new Uint32Array(3);
                const namePtr = this.module.allocateUTF8(moduleName);
                
                this.module._mp_js_do_import(namePtr, out.byteOffset);
                this.module._free(namePtr);
                
                if (out[0] === 0) {
                    resolve({
                        success: true,
                        module: out[1]
                    });
                } else {
                    reject({
                        success: false,
                        error: `Failed to import ${moduleName}`,
                        exception: out[1]
                    });
                }
            } catch (error) {
                reject({
                    success: false,
                    error: error.message
                });
            }
        });
    }
    
    /**
     * REPL interaction
     */
    processReplChar(char) {
        if (!this.initialized) {
            throw new Error('CircuitPython not initialized. Call init() first.');
        }
        
        return this.module._mp_js_repl_process_char(char.charCodeAt(0));
    }
    
    /**
     * Process a complete REPL line
     */
    async processReplLine(line) {
        const results = [];
        for (const char of line) {
            results.push(this.processReplChar(char));
        }
        results.push(this.processReplChar('\n'));
        
        return {
            complete: results[results.length - 1] === 0,
            output: this.outputBuffer.join(''),
            error: this.errorBuffer.join('')
        };
    }
    
    /**
     * Register JavaScript module for Python import
     */
    registerJsModule(name, jsObject) {
        if (!this.initialized) {
            throw new Error('CircuitPython not initialized. Call init() first.');
        }
        
        const namePtr = this.module.allocateUTF8(name);
        const objRef = this.module.proxy_js_ref(jsObject);
        
        this.module._mp_js_register_js_module(namePtr, objRef);
        this.module._free(namePtr);
    }
    
    /**
     * Clean up resources
     */
    dispose() {
        if (this.module) {
            // Clean up any allocated memory
            this.module = null;
        }
        this.initialized = false;
    }
}

/**
 * Factory function for creating CircuitPython instance
 */
export async function createCircuitPython(options = {}) {
    const bridge = new CircuitPythonBridge();
    await bridge.init(options);
    return bridge;
}

/**
 * Web Worker support for non-blocking execution
 */
export class CircuitPythonWorker {
    constructor() {
        this.worker = null;
        this.messageId = 0;
        this.pendingMessages = new Map();
    }
    
    async init() {
        // Create worker from inline code
        const workerCode = `
            import('./circuitpython-bridge.js').then(async ({ createCircuitPython }) => {
                let cp = null;
                
                self.onmessage = async (e) => {
                    const { id, type, data } = e.data;
                    
                    try {
                        let result;
                        
                        switch (type) {
                            case 'init':
                                cp = await createCircuitPython(data);
                                result = { success: true };
                                break;
                                
                            case 'execute':
                                result = await cp.execute(data.code);
                                break;
                                
                            case 'import':
                                result = await cp.importModule(data.module);
                                break;
                                
                            case 'repl':
                                result = await cp.processReplLine(data.line);
                                break;
                                
                            default:
                                throw new Error(\`Unknown message type: \${type}\`);
                        }
                        
                        self.postMessage({ id, success: true, result });
                    } catch (error) {
                        self.postMessage({ id, success: false, error: error.message });
                    }
                };
            });
        `;
        
        const blob = new Blob([workerCode], { type: 'application/javascript' });
        const workerUrl = URL.createObjectURL(blob);
        this.worker = new Worker(workerUrl, { type: 'module' });
        
        // Initialize the worker
        await this.sendMessage('init', {});
        
        return this;
    }
    
    sendMessage(type, data) {
        return new Promise((resolve, reject) => {
            const id = this.messageId++;
            
            this.pendingMessages.set(id, { resolve, reject });
            
            this.worker.onmessage = (e) => {
                const { id, success, result, error } = e.data;
                const pending = this.pendingMessages.get(id);
                
                if (pending) {
                    this.pendingMessages.delete(id);
                    
                    if (success) {
                        pending.resolve(result);
                    } else {
                        pending.reject(new Error(error));
                    }
                }
            };
            
            this.worker.postMessage({ id, type, data });
        });
    }
    
    async execute(code) {
        return this.sendMessage('execute', { code });
    }
    
    async importModule(moduleName) {
        return this.sendMessage('import', { module: moduleName });
    }
    
    async processReplLine(line) {
        return this.sendMessage('repl', { line });
    }
    
    terminate() {
        if (this.worker) {
            this.worker.terminate();
            this.worker = null;
        }
    }
}

// Default export
export default { CircuitPythonBridge, CircuitPythonWorker, createCircuitPython };