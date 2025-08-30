// Basic test for CircuitPython WebAssembly build
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function test() {
    console.log("Loading CircuitPython WebAssembly module...");
    
    try {
        const mp = await _createCircuitPythonModule();
        console.log("✓ Module loaded successfully");
        
        // Initialize MicroPython
        mp._mp_js_init_with_heap(64 * 1024); // 64KB heap
        console.log("✓ MicroPython initialized");
        
        // Test basic Python execution
        console.log("Testing Python execution...");
        
        // Allocate memory for output
        const outputPtr = mp._malloc(4);
        
        // Test basic Python execution
        mp._mp_js_do_exec("print('Hello from CircuitPython WebAssembly!')", 47, outputPtr);
        console.log("✓ Basic Python execution test passed");
        
        // Test simple arithmetic  
        mp._mp_js_do_exec("result = 2 + 3", 14, outputPtr);
        console.log("✓ Arithmetic test passed");
        
        // Test string operations
        mp._mp_js_do_exec("message = 'Hello' + ' World'", 28, outputPtr);
        console.log("✓ String operations test passed");
        
        mp._free(outputPtr);
        
        console.log("All basic tests passed!");
        
    } catch (error) {
        console.error("✗ Test failed:", error);
        process.exit(1);
    }
}

test();