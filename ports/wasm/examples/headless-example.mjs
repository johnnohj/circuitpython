#!/usr/bin/env node
/**
 * CircuitPython WASM + xterm.js Headless Integration Example
 * 
 * This demonstrates the proper I/O wiring between CircuitPython WASM
 * and xterm.js headless for terminal applications.
 * 
 * Run: npm install && npm start
 */

import pkg from '@xterm/headless';
const { Terminal } = pkg;
import _createCircuitPythonModule from '../build/circuitpython.mjs';

class CircuitPythonTerminal {
    constructor() {
        this.terminal = new Terminal({
            cols: 80,
            rows: 24,
            allowProposedApi: true
        });
        
        this.circuitPython = null;
        this.outputBuffer = '';
        this.inputBuffer = '';
        this.isInitialized = false;
    }

    async initialize() {
        console.log('Initializing CircuitPython WASM...');
        
        // Load CircuitPython WASM with proper I/O handlers
        this.circuitPython = await _createCircuitPythonModule({
            // Capture stdout from WASM and send to terminal
            stdout: (charCode) => {
                const char = String.fromCharCode(charCode);
                this.terminal.write(char);
                this.outputBuffer += char;
            },
            
            // Capture stderr from WASM and send to terminal
            stderr: (charCode) => {
                const char = String.fromCharCode(charCode);
                this.terminal.write(char);
                this.outputBuffer += char;
            }
        });

        // Initialize CircuitPython with proper heap size
        this.circuitPython._mp_js_init_with_heap(512 * 1024); // 512KB heap
        console.log('✓ CircuitPython initialized');

        // Initialize REPL
        this.circuitPython._mp_js_repl_init();
        console.log('✓ REPL ready');

        // Set up terminal input handling
        this.terminal.onData((data) => {
            this.handleInput(data);
        });

        this.isInitialized = true;
        console.log('✓ Terminal integration ready');
        
        // Show current terminal state
        this.showTerminalState();
    }

    handleInput(data) {
        if (!this.isInitialized || !this.circuitPython) return;

        // Process each character through CircuitPython REPL
        for (let i = 0; i < data.length; i++) {
            const charCode = data.charCodeAt(i);
            
            // Special handling for common terminal sequences
            if (charCode === 13) { // Enter key
                // Add to input buffer for tracking
                this.inputBuffer += data[i];
                
                // Process through CircuitPython REPL
                const result = this.circuitPython._mp_js_repl_process_char(charCode);
                
                // Clear input buffer after processing line
                this.inputBuffer = '';
                
            } else if (charCode === 3) { // Ctrl+C
                this.inputBuffer = '';
                this.circuitPython._mp_js_repl_process_char(charCode);
                
            } else if (charCode === 127 || charCode === 8) { // Backspace/DEL
                if (this.inputBuffer.length > 0) {
                    this.inputBuffer = this.inputBuffer.slice(0, -1);
                }
                this.circuitPython._mp_js_repl_process_char(charCode);
                
            } else {
                // Regular character
                this.inputBuffer += data[i];
                this.circuitPython._mp_js_repl_process_char(charCode);
            }
        }
    }

    // Demonstrate dynamic module loading through terminal
    async loadPythonModule(moduleName, sourceCode) {
        if (!this.circuitPython._mp_js_load_module) {
            this.terminal.write('\\r\\n⚠️  Module loading not available\\r\\n');
            return false;
        }

        const result = this.circuitPython._mp_js_load_module(moduleName, sourceCode);
        
        if (result === 0) {
            this.terminal.write(`\\r\\n✓ Module '${moduleName}' loaded successfully\\r\\n`);
            return true;
        } else {
            this.terminal.write(`\\r\\n❌ Failed to load module '${moduleName}' (error: ${result})\\r\\n`);
            return false;
        }
    }

    // Show current terminal state for debugging
    showTerminalState() {
        console.log('\\n=== Terminal State ===');
        console.log(`Dimensions: ${this.terminal.cols}x${this.terminal.rows}`);
        console.log(`Cursor: (${this.terminal.buffer.active.cursorX}, ${this.terminal.buffer.active.cursorY})`);
        console.log(`Output buffer length: ${this.outputBuffer.length}`);
        console.log(`Input buffer: "${this.inputBuffer}"`);
        
        // Get terminal content
        const content = [];
        for (let i = 0; i < this.terminal.buffer.active.length; i++) {
            const line = this.terminal.buffer.active.getLine(i);
            if (line) {
                content.push(line.translateToString());
            }
        }
        
        console.log('Terminal content:');
        content.forEach((line, i) => {
            if (line.trim()) {
                console.log(`  ${i}: "${line}"`);
            }
        });
    }

    // Simulate interactive session
    async runDemo() {
        console.log('\\n=== Running Interactive Demo ===');
        
        // Simulate typing Python commands
        const commands = [
            'help()',
            '2 + 3',
            'x = "Hello from CircuitPython WASM!"',
            'print(x)',
            'import sys',
            'sys.implementation',
            'dir()'
        ];

        for (const cmd of commands) {
            console.log(`\\nSimulating command: ${cmd}`);
            
            // Type the command character by character (simulating user input)
            for (const char of cmd) {
                this.handleInput(char);
                // Small delay to simulate typing
                await new Promise(resolve => setTimeout(resolve, 50));
            }
            
            // Press Enter
            this.handleInput('\\r');
            
            // Wait for processing
            await new Promise(resolve => setTimeout(resolve, 500));
            
            // Show state after each command
            this.showTerminalState();
        }

        // Test dynamic module loading
        console.log('\\nTesting dynamic module loading...');
        const testModule = `
def greet(name):
    return f"Hello {name} from dynamically loaded module!"

def math_demo():
    return [i**2 for i in range(5)]

print("Test module loaded and ready!")
        `;

        await this.loadPythonModule('demo_module', testModule);
        
        // Try to use the loaded module
        console.log('\\nTrying to use loaded module...');
        this.handleInput('greet("xterm.js")');
        this.handleInput('\\r');
        
        await new Promise(resolve => setTimeout(resolve, 500));
        this.showTerminalState();
    }
}

// Main execution
async function main() {
    console.log('CircuitPython WASM + xterm.js Headless Integration Example');
    console.log('='.repeat(60));
    
    const terminal = new CircuitPythonTerminal();
    
    try {
        await terminal.initialize();
        await terminal.runDemo();
        
        console.log('\\n✅ Demo completed successfully!');
        console.log('\\nThis example demonstrates:');
        console.log('• Proper WASM ↔ xterm.js I/O wiring');
        console.log('• Character-by-character REPL processing');
        console.log('• Terminal state management');
        console.log('• Dynamic module loading integration');
        console.log('• Headless terminal operation');
        
    } catch (error) {
        console.error('❌ Error:', error);
        process.exit(1);
    }
}

main();