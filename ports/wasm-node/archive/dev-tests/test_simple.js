#!/usr/bin/env node

// Simple test using low-level Emscripten API directly
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

async function testSimple() {
    console.log('Loading CircuitPython WebAssembly module...');
    
    const moduleFile = path.join(__dirname, 'build-standard', 'circuitpython.mjs');
    
    try {
        // Import the raw Emscripten module
        const createModule = (await import(moduleFile)).default;
        
        console.log('Creating module...');
        const Module = await createModule();
        
        console.log('Initializing CircuitPython runtime...');
        // Initialize with heap
        Module.ccall('mp_js_init_with_heap', 'null', ['number'], [1024 * 1024]);
        
        console.log('Testing sys.path access...');
        // Test Python code execution without print
        const code = 'import sys; len(sys.path)';
        const len = Module.lengthBytesUTF8(code);
        const buf = Module._malloc(len + 1);
        Module.stringToUTF8(code, buf, len + 1);
        const value = Module._malloc(3 * 4);
        
        try {
            const result = Module.ccall(
                'mp_js_do_exec',
                'number',
                ['pointer', 'number', 'pointer'],
                [buf, len, value]
            );
            console.log('✅ sys.path access successful - no memory errors!');
        } catch (error) {
            console.log('❌ sys.path access failed:', error.message);
        }
        
        Module._free(buf);
        Module._free(value);
        
        console.log('Testing simple import...');
        // Test basic import without print
        const importCode = 'import math; math.pi';
        const importLen = Module.lengthBytesUTF8(importCode);
        const importBuf = Module._malloc(importLen + 1);
        Module.stringToUTF8(importCode, importBuf, importLen + 1);
        const importValue = Module._malloc(3 * 4);
        
        try {
            Module.ccall(
                'mp_js_do_exec',
                'number',
                ['pointer', 'number', 'pointer'],
                [importBuf, importLen, importValue]
            );
            console.log('✅ Math import successful - no memory errors!');
        } catch (error) {
            console.log('❌ Math import failed:', error.message);
        }
        
        Module._free(importBuf);
        Module._free(importValue);
        
        console.log('Testing collections import...');
        // Test collections import
        const collCode = 'import collections; collections';
        const collLen = Module.lengthBytesUTF8(collCode);
        const collBuf = Module._malloc(collLen + 1);
        Module.stringToUTF8(collCode, collBuf, collLen + 1);
        const collValue = Module._malloc(3 * 4);
        
        try {
            Module.ccall(
                'mp_js_do_exec',
                'number',
                ['pointer', 'number', 'pointer'],
                [collBuf, collLen, collValue]
            );
            console.log('✅ Collections import successful - no memory errors!');
        } catch (error) {
            console.log('❌ Collections import failed:', error.message);
        }
        
        Module._free(collBuf);
        Module._free(collValue);
        
        console.log('All tests completed!');
        
    } catch (error) {
        console.error('Error during testing:', error);
        console.error('Stack trace:', error.stack);
        process.exit(1);
    }
}

testSimple().catch(console.error);