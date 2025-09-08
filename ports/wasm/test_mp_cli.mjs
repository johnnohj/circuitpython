#!/usr/bin/env node

// Test MicroPython CLI directly from built module

import _createMicroPythonModule from './build-standard/micropython.mjs';

async function testMicroPythonCLI() {
    console.log('Loading MicroPython...');
    
    const Module = await _createMicroPythonModule({
        stdout: (data) => process.stdout.write(data),
        linebuffer: false,
    });
    
    console.log('Initializing...');
    Module._mp_js_init();
    
    console.log('Initializing REPL...');
    Module._mp_js_repl_init();
    
    console.log('REPL should show prompt now:');
    await new Promise(resolve => setTimeout(resolve, 100));
    
    console.log('\nSending test input "2+3\\n":');
    
    // Test with "2+3\n"
    const testInput = "2+3\n";
    for (let i = 0; i < testInput.length; i++) {
        const charCode = testInput.charCodeAt(i);
        const result = Module._mp_js_repl_process_char(charCode);
        
        if (result) {
            console.log('\nREPL wants to exit');
            break;
        }
        
        await new Promise(resolve => setTimeout(resolve, 10));
    }
    
    console.log('\nTest completed!');
}

testMicroPythonCLI().catch(console.error);