#!/usr/bin/env node
/**
 * CircuitPython Node.js Runtime
 *
 * A focused Node.js entry point for CircuitPython execution in CLI tools,
 * automation scripts, background processes, and any Node.js runtime environment.
 * Optimized for scripting, automation, CLI applications, and integration workflows.
 */

import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);
const require = createRequire(import.meta.url);

// Import Node.js-specific hardware bridge for server environments
import { NodeJSHardwareBridge } from './nodejs-hardware-bridge.js';

/**
 * Initialize CircuitPython for Node.js runtime environments
 * @param {Object} options - Configuration options
 * @param {number} options.heapSize - Python heap size in bytes (default: 16MB for CLI/automation)
 * @param {boolean} options.enableHardwareSimulation - Enable GPIO/I2C/SPI simulation (default: true)
 * @param {Object} options.logger - Custom logger (default: console)
 * @returns {Promise<Object>} CircuitPython runtime instance
 */
export async function initCircuitPython(options = {}) {
    const {
        heapSize = 16 * 1024 * 1024, // 16MB default for CLI/background tasks
        enableHardwareSimulation = true,
        logger = console
    } = options;

    try {
        // Load the WebAssembly module
        const createModule = await import('./build-standard/circuitpython.mjs');

        // Initialize with Node.js optimizations
        const Module = await createModule.default({
            print: (text) => logger.log(text),
            printErr: (text) => logger.error(text),
        });

        // Initialize Python runtime with specified heap
        Module._mp_js_init_with_heap(heapSize);
        Module._proxy_c_init();

        // Setup hardware simulation for CLI/automation use cases
        if (enableHardwareSimulation) {
            const hardwareBridge = new NodeJSHardwareBridge();
            global.board = hardwareBridge.createBoard();
        }

        // Initialize REPL for interactive use
        Module._mp_js_repl_init();

        return {
            // Core execution methods
            runCode: (pythonCode) => {
                const output = new Uint32Array(3);
                return Module._mp_js_do_exec(pythonCode, pythonCode.length, output);
            },

            // Interactive REPL
            repl: {
                processChar: (char) => Module._mp_js_repl_process_char(char.charCodeAt(0)),
                processString: (input) => {
                    for (const char of input) {
                        Module._mp_js_repl_process_char(char.charCodeAt(0));
                    }
                }
            },

            // CLI and automation utilities
            interrupt: () => Module._mp_hal_set_interrupt_char(3), // Ctrl-C
            isInterrupted: () => Module._mp_hal_get_interrupt_char() === 3,

            // Hardware access (if enabled)
            hardware: enableHardwareSimulation ? global.board : null,

            // Module management
            registerJSModule: (name, obj) => Module._mp_js_register_js_module(name, obj),

            // Clean shutdown for CLI applications and background tasks
            shutdown: () => {
                if (enableHardwareSimulation && global.board) {
                    // Cleanup hardware resources
                    Object.values(global.board).forEach(pin => {
                        if (pin.digitalIO && pin.digitalIO.deinit) pin.digitalIO.deinit();
                        if (pin.analogIO && pin.analogIO.deinit) pin.analogIO.deinit();
                    });
                }
                Module._mp_js_deinit();
            }
        };
    } catch (error) {
        logger.error('Failed to initialize CircuitPython:', error);
        throw error;
    }
}

/**
 * Start an interactive REPL session
 * Useful for development and testing
 */
export async function startREPL(options = {}) {
    const cp = await initCircuitPython(options);

    process.stdout.write('CircuitPython Node.js REPL\nType Ctrl+C to exit\n\n');

    // Handle input from stdin
    process.stdin.setRawMode(true);
    process.stdin.resume();
    process.stdin.setEncoding('utf8');

    process.stdin.on('data', (key) => {
        if (key === '\u0003') { // Ctrl+C
            cp.shutdown();
            process.exit();
        }
        cp.repl.processString(key);
    });

    // Handle graceful shutdown
    process.on('SIGINT', () => {
        cp.shutdown();
        process.exit();
    });
}

// If called directly, start REPL
if (process.argv[1] === __filename) {
    startREPL().catch(console.error);
}