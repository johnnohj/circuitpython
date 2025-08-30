/*
 * CircuitPython WebAssembly Web Worker API
 * Web Worker-specific functionality for browser environments
 */

// Web Worker message handler setup
function setupWorkerMessageHandler(circuitPythonOptions = {}) {
    let cp = null;
    
    self.onmessage = async (event) => {
        const { action, code, options } = event.data;
        
        try {
            // Initialize CircuitPython on first use
            if (!cp) {
                cp = await loadCircuitPython({
                    heapsize: 512 * 1024, // 512KB default for workers
                    stdout: (line) => {
                        self.postMessage({
                            type: 'stdout',
                            data: line
                        });
                    },
                    stderr: (line) => {
                        self.postMessage({
                            type: 'stderr', 
                            data: line
                        });
                    },
                    ...circuitPythonOptions,
                    ...options
                });
            }
            
            switch (action) {
                case 'execute':
                    const result = await cp.runPythonAsync(code);
                    self.postMessage({
                        type: 'result',
                        success: true,
                        data: result
                    });
                    break;
                    
                case 'import':
                    const module = cp.pyimport(code);
                    self.postMessage({
                        type: 'result',
                        success: true,
                        data: module
                    });
                    break;
                    
                default:
                    self.postMessage({
                        type: 'error',
                        success: false,
                        error: `Unknown action: ${action}`
                    });
            }
            
        } catch (error) {
            self.postMessage({
                type: 'error',
                success: false,
                error: error.message || error.toString()
            });
        }
    };
}

// Helper for main thread to create worker communication
function createWorkerInterface(workerScript) {
    const worker = new Worker(workerScript);
    
    return {
        execute(code) {
            return new Promise((resolve, reject) => {
                const messageHandler = (event) => {
                    const { type, success, data, error } = event.data;
                    
                    if (type === 'result') {
                        worker.removeEventListener('message', messageHandler);
                        if (success) {
                            resolve(data);
                        } else {
                            reject(new Error(error));
                        }
                    } else if (type === 'error') {
                        worker.removeEventListener('message', messageHandler);
                        reject(new Error(error));
                    }
                    // Ignore stdout/stderr messages for simple interface
                };
                
                worker.addEventListener('message', messageHandler);
                worker.postMessage({
                    action: 'execute',
                    code: code
                });
            });
        },
        
        pyimport(moduleName) {
            return new Promise((resolve, reject) => {
                const messageHandler = (event) => {
                    const { type, success, data, error } = event.data;
                    
                    if (type === 'result') {
                        worker.removeEventListener('message', messageHandler);
                        if (success) {
                            resolve(data);
                        } else {
                            reject(new Error(error));
                        }
                    } else if (type === 'error') {
                        worker.removeEventListener('message', messageHandler);
                        reject(new Error(error));
                    }
                };
                
                worker.addEventListener('message', messageHandler);
                worker.postMessage({
                    action: 'import',
                    code: moduleName
                });
            });
        },
        
        terminate() {
            worker.terminate();
        }
    };
}

export { setupWorkerMessageHandler, createWorkerInterface };