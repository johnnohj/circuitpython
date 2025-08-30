#!/usr/bin/env node

// Test REPL by "kicking" it to show prompt

import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testREPLKick() {
    console.log('Loading CircuitPython...');
    
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
    
    console.log('Sending initial newline to trigger prompt:');
    
    // Send an initial newline to "kick" the REPL
    let result = Module._mp_js_repl_process_char(13); // \r (carriage return)
    console.log(`Result from \\r: ${result}`);
    
    await new Promise(resolve => setTimeout(resolve, 100));
    
    result = Module._mp_js_repl_process_char(10); // \n (newline)
    console.log(`Result from \\n: ${result}`);
    
    await new Promise(resolve => setTimeout(resolve, 100));
    
    // Now try to send "2+3\n"
    console.log('\nSending "2+3":');
    const testInput = "2+3\n";
    
    for (let i = 0; i < testInput.length; i++) {
        const char = testInput[i];
        const charCode = testInput.charCodeAt(i);
        
        console.log(`Sending: '${char}' (${charCode})`);
        result = Module._mp_js_repl_process_char(charCode);
        console.log(`Result: ${result}`);
        
        await new Promise(resolve => setTimeout(resolve, 50));
    }
    
    console.log('\nTest completed!');
}

testREPLKick().catch(console.error);