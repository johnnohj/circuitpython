#!/usr/bin/env node

/**
 * Test the raw CircuitPython WebAssembly module without high-level API wrappers
 */

import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testRawModule() {
    console.log('Loading CircuitPython WebAssembly module...');
    
    try {
        // Load the module with basic configuration
        const Module = await _createCircuitPythonModule({
            noInitialRun: true,
            stdout: (c) => process.stdout.write(String.fromCharCode(c)),
            stderr: (c) => process.stderr.write(String.fromCharCode(c))
        });
        
        console.log('Module loaded successfully!');
        console.log('Available functions:', Object.keys(Module).filter(k => k.startsWith('_mp')).slice(0, 10));
        
        // Initialize CircuitPython with basic heap size (1MB)
        console.log('Initializing CircuitPython...');
        Module._mp_js_init_with_heap(1024 * 1024);
        console.log('CircuitPython initialized!');
        
        // Test basic Python execution
        console.log('\nTesting Python execution:');
        const code = 'print("Hello from CircuitPython WebAssembly!")';
        const len = code.length;
        
        // Allocate memory for the code string
        const buf = Module._malloc(len + 1);
        Module.stringToUTF8(code, buf, len + 1);
        
        // Allocate memory for the result
        const result_ptr = Module._malloc(3 * 4); // 3 int32s
        
        // Execute the Python code
        Module._mp_js_do_exec(buf, len, result_ptr);
        
        // Clean up memory
        Module._free(buf);
        Module._free(result_ptr);
        
        console.log('Python execution completed!');
        
        // Test REPL initialization
        console.log('\nTesting REPL initialization...');
        Module._mp_js_repl_init();
        console.log('REPL initialized!');
        
        // Test importing a module
        console.log('\nTesting module import:');
        const import_result_ptr = Module._malloc(3 * 4);
        Module._mp_js_do_import('sys', import_result_ptr);
        Module._free(import_result_ptr);
        console.log('Module import completed!');
        
        // Test more complex Python code
        console.log('\nTesting complex Python execution:');
        const complex_code = `
import sys
print(f"CircuitPython version: {sys.version}")
print(f"Platform: {sys.platform}")

# Test basic arithmetic
result = 2 + 3 * 4
print(f"2 + 3 * 4 = {result}")

# Test list comprehension
squares = [x*x for x in range(5)]
print(f"Squares: {squares}")
        `.trim();
        
        const complex_len = complex_code.length;
        const complex_buf = Module._malloc(complex_len + 1);
        Module.stringToUTF8(complex_code, complex_buf, complex_len + 1);
        const complex_result_ptr = Module._malloc(3 * 4);
        
        Module._mp_js_do_exec(complex_buf, complex_len, complex_result_ptr);
        
        Module._free(complex_buf);
        Module._free(complex_result_ptr);
        
        console.log('\nAll tests completed successfully!');
        
    } catch (error) {
        console.error('Error during testing:', error);
        console.error('Stack:', error.stack);
    }
}

// Run the test
testRawModule().catch(console.error);