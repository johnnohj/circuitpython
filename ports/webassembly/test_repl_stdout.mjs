#!/usr/bin/env node

// Test REPL with proper stdout setup

import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testREPLWithStdout() {
    console.log('Loading CircuitPython with stdout configuration...');
    
    const Module = await _createCircuitPythonModule({
        stdout: (c) => {
            process.stdout.write(String.fromCharCode(c));
        },
        stderr: (c) => {
            process.stderr.write(String.fromCharCode(c));
        }
    });
    
    console.log('Initializing...');
    Module._mp_js_init_with_heap(1024 * 1024);
    
    console.log('Initializing REPL...');
    Module._mp_js_repl_init();
    
    console.log('You should see a REPL prompt now:');
    
    // Test sending "2+3\n"
    const testInput = "2+3\n";
    
    for (let i = 0; i < testInput.length; i++) {
        const charCode = testInput.charCodeAt(i);
        const result = Module._mp_js_repl_process_char(charCode);
        
        if (result) {
            console.log('\nREPL wants to exit');
            break;
        }
        
        // Small delay to see output
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    
    console.log('\nTest completed!');
}

testREPLWithStdout().catch(console.error);