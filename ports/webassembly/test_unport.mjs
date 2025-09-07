#!/usr/bin/env node

// Test script for CircuitPython WebAssembly port
import loadCircuitPython from './build-standard/circuitpython.mjs';

async function testWebAssembly() {
    console.log('Loading CircuitPython WebAssembly port...');
    
    try {
        // Create the CircuitPython module
        const mp = await loadCircuitPython({
            print: (text) => console.log(text),
            printErr: (text) => console.error(text),
            // Disable stdin for now
            stdin: () => null
        });
        
        console.log('CircuitPython loaded successfully');
        
        // Initialize with 256KB heap
        console.log('Initializing with 256KB heap...');
        mp._mp_js_init(8 * 1024, 256 * 1024);
        
        console.log('Python initialized');
        
        // Test simple execution
        console.log('\nTesting simple print:');
        const outputPtr = mp._malloc(4);
        mp._mp_js_do_exec("print('Hello from CircuitPython WebAssembly port!')", outputPtr);
        mp._free(outputPtr);
        
        // Test import
        console.log('\nTesting import:');
        const importPtr = mp._malloc(4);
        mp._mp_js_do_exec("import sys; print('Python version:', sys.version)", importPtr);
        mp._free(importPtr);
        
        // Test math operations
        console.log('\nTesting math:');
        const mathPtr = mp._malloc(4);
        mp._mp_js_do_exec("print('2 + 2 =', 2 + 2)", mathPtr);
        mp._free(mathPtr);
        
        console.log('\n✅ All tests passed!');
        
    } catch (error) {
        console.error('❌ Error:', error);
        console.error(error.stack);
    }
}

testWebAssembly();