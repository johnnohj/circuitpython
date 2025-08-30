#!/usr/bin/env node

// Debug the REPL character processing

import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testREPLDebug() {
    console.log('Loading CircuitPython...');
    const Module = await _createCircuitPythonModule();
    
    console.log('Initializing...');
    Module._mp_js_init_with_heap(1024 * 1024);
    
    console.log('Initializing REPL...');
    Module._mp_js_repl_init();
    
    console.log('Testing character processing...');
    
    // Test sending individual characters for "2+3\n"
    const testInput = "2+3\n";
    
    for (let i = 0; i < testInput.length; i++) {
        const char = testInput[i];
        const charCode = testInput.charCodeAt(i);
        
        console.log(`Sending character: '${char}' (code: ${charCode})`);
        
        try {
            const result = Module._mp_js_repl_process_char(charCode);
            console.log(`REPL result: ${result}`);
        } catch (error) {
            console.error(`Error processing character '${char}':`, error);
        }
    }
    
    console.log('Test completed!');
}

testREPLDebug().catch(console.error);