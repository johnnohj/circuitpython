#!/usr/bin/env node

// Quick test to verify the CircuitPython WASM build works
import { readFileSync } from 'fs';

try {
    console.log('Loading CircuitPython WASM module...');
    const wasmBytes = readFileSync('./build/circuitpython.wasm');
    console.log(`WASM file size: ${wasmBytes.length} bytes`);
    
    const wasmModule = await WebAssembly.compile(wasmBytes);
    console.log('✅ WASM module compiled successfully!');
    
    // Check exports
    const imports = WebAssembly.Module.imports(wasmModule);
    const exports = WebAssembly.Module.exports(wasmModule);
    
    console.log(`Imports: ${imports.length} functions/globals needed`);
    console.log(`Exports: ${exports.length} functions/globals provided`);
    
    console.log('\nKey exports:');
    exports.slice(0, 10).forEach(exp => {
        console.log(`  - ${exp.name} (${exp.kind})`);
    });
    
    if (exports.length > 10) {
        console.log(`  ... and ${exports.length - 10} more`);
    }
    
    // Look for key CircuitPython functions
    const keyFunctions = ['mp_js_init', 'mp_js_init_with_heap', 'mp_js_repl_init', 'mp_js_repl_process_char'];
    console.log('\nKey CircuitPython functions:');
    keyFunctions.forEach(name => {
        const found = exports.find(exp => exp.name === name);
        console.log(`  - ${name}: ${found ? '✅ Found' : '❌ Missing'}`);
    });
    
    console.log('\n🎉 CircuitPython WebAssembly build appears successful!');
    
} catch (error) {
    console.error('❌ Error testing WASM module:', error);
    process.exit(1);
}