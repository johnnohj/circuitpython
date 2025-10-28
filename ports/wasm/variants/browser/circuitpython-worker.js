/*
 * CircuitPython Web Worker
 *
 * This worker runs CircuitPython WASM in a background thread to prevent
 * blocking the main UI thread during execution (e.g., during time.sleep()).
 *
 * Messages from main thread:
 * - { type: 'init', options: {...} } - Initialize CircuitPython
 * - { type: 'runPython', code: '...' } - Execute Python code
 * - { type: 'replInit' } - Initialize REPL
 * - { type: 'replProcessChar', char: 65 } - Process character in REPL
 * - { type: 'writeFile', path: '/code.py', content: '...' } - Write file
 * - { type: 'readFile', path: '/code.py' } - Read file
 * - { type: 'runFile', path: '/code.py' } - Execute file
 *
 * Messages to main thread:
 * - { type: 'stdout', data: '...' } - Output text
 * - { type: 'stderr', data: '...' } - Error text
 * - { type: 'ready' } - Worker initialized
 * - { type: 'result', id: 123, result: ... } - Command result
 * - { type: 'error', id: 123, error: ... } - Command error
 */

let circuitPython = null;
let nextMessageId = 0;

// Handle messages from main thread
self.onmessage = async function(e) {
    const { type, id, ...params } = e.data;

    try {
        switch (type) {
            case 'init':
                await initCircuitPython(params.options);
                postMessage({ type: 'ready' });
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result: true });
                }
                break;

            case 'runPython':
                const result = circuitPython.runPython(params.code);
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result });
                }
                break;

            case 'replInit':
                circuitPython.replInit();
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result: null });
                }
                break;

            case 'replProcessChar':
                const charResult = circuitPython.replProcessChar(params.char);
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result: charResult });
                }
                break;

            case 'writeFile':
                if (circuitPython.saveFile) {
                    await circuitPython.saveFile(params.path, params.content);
                } else {
                    circuitPython.FS.writeFile(params.path, params.content);
                }
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result: true });
                }
                break;

            case 'readFile':
                let content;
                if (circuitPython.FS.analyzePath(params.path).exists) {
                    content = circuitPython.FS.readFile(params.path, { encoding: 'utf8' });
                } else {
                    throw new Error(`File not found: ${params.path}`);
                }
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result: content });
                }
                break;

            case 'runFile':
                const fileResult = circuitPython.runFile(params.path);
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result: fileResult });
                }
                break;

            case 'fileExists':
                const exists = circuitPython.FS.analyzePath(params.path).exists;
                if (id !== undefined) {
                    postMessage({ type: 'result', id, result: exists });
                }
                break;

            default:
                throw new Error(`Unknown message type: ${type}`);
        }
    } catch (error) {
        if (id !== undefined) {
            postMessage({
                type: 'error',
                id,
                error: {
                    message: error.message,
                    stack: error.stack,
                    name: error.name
                }
            });
        } else {
            // Unhandled error without ID
            console.error('Worker error:', error);
            postMessage({ type: 'stderr', data: `Worker error: ${error.message}\n` });
        }
    }
};

async function initCircuitPython(options) {
    // Import the CircuitPython module (it's concatenated in the .mjs file)
    // In a worker, we need to import it
    const module = await import('./circuitpython.mjs');
    const { loadCircuitPython } = module;

    // Initialize with stdout/stderr callbacks that post messages
    circuitPython = await loadCircuitPython({
        ...options,
        stdout: (charArray) => {
            const text = new TextDecoder().decode(charArray);
            postMessage({ type: 'stdout', data: text });
        },
        stderr: (charArray) => {
            const text = new TextDecoder().decode(charArray);
            postMessage({ type: 'stderr', data: text });
        },
        // Worker environment doesn't have verbose output issues
        verbose: options.verbose !== undefined ? options.verbose : false,
    });

    console.log('[Worker] CircuitPython initialized');
}

// Send a message that we're ready to receive init
postMessage({ type: 'worker-loaded' });
