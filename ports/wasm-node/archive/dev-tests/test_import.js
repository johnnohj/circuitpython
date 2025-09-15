#!/usr/bin/env node

// Test script to verify that module imports work after sys.path fixes
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

async function testImport() {
    console.log('Loading CircuitPython WebAssembly module...');
    
    // Load the compiled WebAssembly module
    const moduleFile = path.join(__dirname, 'build-standard', 'circuitpython.mjs');
    
    if (!fs.existsSync(moduleFile)) {
        console.error('CircuitPython module not found at:', moduleFile);
        console.error('Please run "make VARIANT=standard" first');
        process.exit(1);
    }
    
    try {
        // Import the API module  
        const importedModule = await import(moduleFile);
        console.log('Imported module keys:', Object.keys(importedModule));
        console.log('Default export:', typeof importedModule.default);
        console.log('loadCircuitPython export:', typeof importedModule.loadCircuitPython);
        
        // Try different import patterns
        let loadCircuitPython = importedModule.loadCircuitPython || importedModule.default;
        if (!loadCircuitPython) {
            console.error('Could not find loadCircuitPython function in exports');
            return;
        }
        
        console.log('Initializing CircuitPython...');
        const circuitPython = await loadCircuitPython({
            heapsize: 1024 * 1024, // 1MB heap
            stdout: (data) => process.stdout.write(data),
            stderr: (data) => process.stderr.write(data),
        });
        
        console.log('CircuitPython object:', typeof circuitPython);
        console.log('CircuitPython methods:', Object.keys(circuitPython || {}));
        
        if (!circuitPython || typeof circuitPython.runPython !== 'function') {
            console.error('CircuitPython object does not have runPython method');
            return;
        }
        
        console.log('Testing sys.path initialization...');
        // Test sys.path access
        const sysPathResult = circuitPython.runPython('import sys; print("sys.path:", sys.path); sys.path');
        console.log('sys.path result:', sysPathResult);
        
        console.log('Testing basic module import...');
        // Test a simple import
        const importResult = circuitPython.runPython('import collections; print("collections imported successfully"); collections');
        console.log('Import result:', importResult);
        
        console.log('Testing frozen module import...');
        // Test frozen module import if available
        const frozenResult = circuitPython.runPython(`
try:
    import _boot
    print("_boot imported successfully")
    _boot
except ImportError as e:
    print("_boot not available:", e)
    None
`);
        console.log('Frozen import result:', frozenResult);
        
        console.log('Testing direct module import via pyimport...');
        // Test direct import via pyimport function
        try {
            const mathModule = circuitPython.pyimport('math');
            console.log('math module imported via pyimport:', mathModule);
        } catch (error) {
            console.log('pyimport error:', error.message);
        }
        
        console.log('All tests completed successfully!');
        
    } catch (error) {
        console.error('Error during testing:', error);
        console.error('Stack trace:', error.stack);
        process.exit(1);
    }
}

testImport().catch(console.error);