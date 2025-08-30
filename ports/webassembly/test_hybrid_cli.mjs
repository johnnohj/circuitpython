#!/usr/bin/env node

// Test if we can use direct Python execution within the CLI framework

import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testHybridCLI() {
    console.log('Loading CircuitPython...');
    const Module = await _createCircuitPythonModule();
    
    console.log('Initializing...');
    Module._mp_js_init_with_heap(1024 * 1024);
    
    console.log('Testing direct Python execution:');
    
    // Test direct Python execution (this worked in our earlier tests)
    const commands = [
        'print("Hello from CircuitPython!")',
        '2 + 3',
        'import sys',
        'print(f"Python version: {sys.version}")',
        'x = [1, 2, 3, 4, 5]',
        'print(f"List: {x}")',
        'print("Math test:", 10 * 4 + 2)'
    ];
    
    for (const code of commands) {
        console.log(`\n>>> ${code}`);
        
        const len = code.length;
        const buf = Module._malloc(len + 1);
        Module.stringToUTF8(code, buf, len + 1);
        const result_ptr = Module._malloc(12);
        
        try {
            Module._mp_js_do_exec(buf, len, result_ptr);
        } catch (error) {
            console.log(`Error: ${error.message}`);
        } finally {
            Module._free(buf);
            Module._free(result_ptr);
        }
    }
    
    console.log('\nDirect execution test completed!');
}

testHybridCLI().catch(console.error);