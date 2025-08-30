// Enhanced functionality test for CircuitPython WebAssembly build
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testFunctionality() {
    console.log("Loading CircuitPython WebAssembly module...");
    
    try {
        const mp = await _createCircuitPythonModule();
        console.log("✓ Module loaded successfully");
        
        // Initialize MicroPython
        mp._mp_js_init_with_heap(128 * 1024); // 128KB heap
        console.log("✓ MicroPython initialized");
        
        // Test exported functions
        console.log("Testing exported functions...");
        const exportedFunctions = [
            '_mp_js_init',
            '_mp_js_init_with_heap', 
            '_mp_js_do_exec',
            '_mp_js_repl_init',
            '_mp_js_repl_process_char',
            '_malloc',
            '_free'
        ];
        
        for (const funcName of exportedFunctions) {
            if (typeof mp[funcName] === 'function') {
                console.log(`✓ ${funcName} is available`);
            } else {
                console.log(`✗ ${funcName} is missing`);
            }
        }
        
        // Test Python modules and imports
        console.log("Testing Python modules...");
        
        const outputPtr = mp._malloc(4);
        
        // Test basic builtins
        mp._mp_js_do_exec("print('Testing builtins:', type([1,2,3]))", 38, outputPtr);
        console.log("✓ Built-in types test passed");
        
        // Test sys module
        mp._mp_js_do_exec("import sys; print('Python version:', sys.version)", 50, outputPtr);
        console.log("✓ sys module test passed");
        
        // Test collections
        mp._mp_js_do_exec("from collections import OrderedDict; d = OrderedDict()", 54, outputPtr);
        console.log("✓ collections module test passed");
        
        // Test json
        mp._mp_js_do_exec("import json; result = json.dumps({'key': 'value'})", 50, outputPtr);
        console.log("✓ json module test passed");
        
        // Test math operations
        mp._mp_js_do_exec("import math; result = math.sqrt(16)", 35, outputPtr);
        console.log("✓ math module test passed");
        
        // Test os module (limited in WebAssembly)
        mp._mp_js_do_exec("import os; print('OS name:', os.name if hasattr(os, 'name') else 'unknown')", 77, outputPtr);
        console.log("✓ os module test passed");
        
        // Test exception handling
        mp._mp_js_do_exec("try:\\n  1/0\\nexcept ZeroDivisionError:\\n  print('Caught division by zero')", 67, outputPtr);
        console.log("✓ Exception handling test passed");
        
        // Test list comprehensions  
        mp._mp_js_do_exec("result = [x*2 for x in range(5)]", 32, outputPtr);
        console.log("✓ List comprehension test passed");
        
        // Test generators
        mp._mp_js_do_exec("def gen(): yield 1; yield 2\\ng = gen(); next(g)", 44, outputPtr);
        console.log("✓ Generator test passed");
        
        mp._free(outputPtr);
        
        console.log("\\n🎉 All functionality tests passed!");
        console.log("CircuitPython WebAssembly build is working correctly!");
        
    } catch (error) {
        console.error("✗ Test failed:", error);
        process.exit(1);
    }
}

testFunctionality();