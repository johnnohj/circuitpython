#!/usr/bin/env node

// Test the CircuitPython CLI interactively

import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function test() {
    console.log('Loading CircuitPython...');
    const Module = await _createCircuitPythonModule();
    
    console.log('Initializing...');
    Module._mp_js_init_with_heap(1024 * 1024); // 1MB
    
    console.log('Testing simple execution...');
    
    // Test simple code execution
    const code = 'print("Hello from CircuitPython!")';
    const len = code.length;
    const buf = Module._malloc(len + 1);
    Module.stringToUTF8(code, buf, len + 1);
    const result_ptr = Module._malloc(12);
    
    Module._mp_js_do_exec(buf, len, result_ptr);
    
    Module._free(buf);
    Module._free(result_ptr);
    
    console.log('Test completed!');
}

test().catch(console.error);