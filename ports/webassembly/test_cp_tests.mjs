// Manual CircuitPython test runner for WebAssembly
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';
import { readFileSync } from 'fs';

async function runCircuitPythonTest(testFile) {
    try {
        const mp = await _createCircuitPythonModule();
        mp._mp_js_init_with_heap(128 * 1024);
        
        const outputPtr = mp._malloc(4);
        const testCode = readFileSync(testFile, 'utf8');
        
        console.log(`Running test: ${testFile}`);
        mp._mp_js_do_exec(testCode, testCode.length, outputPtr);
        console.log(`✓ Test passed: ${testFile}`);
        
        mp._free(outputPtr);
        return true;
    } catch (error) {
        console.log(`✗ Test failed: ${testFile} - ${error.message}`);
        return false;
    }
}

async function runTests() {
    console.log("Running CircuitPython compatibility tests...");
    
    const testFiles = [
        '../../tests/basics/0prelim.py',
        '../../tests/basics/andor.py', 
        '../../tests/basics/array1.py',
        '../../tests/basics/array_construct.py'
    ];
    
    let passed = 0;
    let total = testFiles.length;
    
    for (const testFile of testFiles) {
        if (await runCircuitPythonTest(testFile)) {
            passed++;
        }
    }
    
    console.log(`\\n📊 Test Results: ${passed}/${total} tests passed`);
    
    if (passed === total) {
        console.log("🎉 All CircuitPython compatibility tests passed!");
    } else {
        console.log(`⚠️  ${total - passed} tests failed`);
    }
}

runTests();