#!/usr/bin/env node

/**
 * Module Import Testing Script  
 * Tests if the critical memory access bug in module imports is fixed
 */

console.log('=== Module Import Test ===\n');

async function testSystemModuleImport() {
    console.log('Test: Import sys module...');
    
    let output = '';
    let hasError = false;
    
    try {
        const CircuitPython = await import('./build-standard/circuitpython.mjs');
        
        const mp = await CircuitPython.default({
            print: (text) => {
                output += text + '\n';
                console.log('[PRINT]', text);
            },
            printErr: (text) => {
                output += '[ERR] ' + text + '\n';
                console.error('[PRINTERR]', text);
            },
            stdout: (charCode) => {
                const char = String.fromCharCode(charCode);
                output += char;
                process.stdout.write(char);
            },
            stderr: (charCode) => {
                hasError = true;
                const char = String.fromCharCode(charCode);
                output += '[STDERR]' + char;
                process.stderr.write('[STDERR]' + char);
            }
        });
        
        // Initialize with our fixes
        mp._mp_js_init_with_heap(8 * 1024 * 1024);
        
        // Try the import that was previously crashing
        const out = new Uint32Array(3);
        const code = 'import sys; print("sys imported successfully")';
        const codePtr = mp.allocateUTF8(code);
        
        console.log('Executing:', code);
        mp._mp_js_do_exec(codePtr, code.length, out.byteOffset);
        mp._free(codePtr);
        
        if (hasError) {
            console.log('❌ Module import failed with errors');
            return false;
        } else {
            console.log('✅ Module import succeeded!');
            return true;
        }
        
    } catch (error) {
        console.error('❌ Module import test failed with exception:', error.message);
        return false;
    }
}

async function testREPLImport() {
    console.log('\nTest: REPL-based import...');
    
    try {
        const CircuitPython = await import('./build-standard/circuitpython.mjs');
        
        let output = '';
        const mp = await CircuitPython.default({
            print: (text) => { 
                output += text + '\n';
                console.log('[REPL-OUT]', text); 
            },
            printErr: (text) => { 
                output += '[ERR] ' + text + '\n';
                console.error('[REPL-ERR]', text); 
            },
            stdout: (charCode) => { 
                const char = String.fromCharCode(charCode);
                output += char;
                if (char !== '\n') process.stdout.write(char);
            }
        });
        
        mp._mp_js_init_with_heap(8 * 1024 * 1024);
        mp._mp_js_repl_init();
        
        // Send import command through REPL
        const cmd = 'import sys';
        console.log('REPL command:', cmd);
        
        for (let i = 0; i < cmd.length; i++) {
            mp._mp_js_repl_process_char(cmd.charCodeAt(i));
        }
        mp._mp_js_repl_process_char(13); // Enter key
        
        // Give it a moment to process
        await new Promise(resolve => setTimeout(resolve, 100));
        
        console.log('✅ REPL import completed without crash');
        return true;
        
    } catch (error) {
        console.error('❌ REPL import test failed:', error.message);
        return false;
    }
}

async function runImportTests() {
    console.log('Testing module imports that previously caused crashes...\n');
    
    const results = [];
    
    results.push(await testSystemModuleImport());
    results.push(await testREPLImport());
    
    const passed = results.filter(r => r).length;
    const total = results.length;
    
    console.log('\n=== Import Test Results ===');
    console.log(`Passed: ${passed}/${total}`);
    
    if (passed === total) {
        console.log('🎉 All import tests passed! Module import crash is FIXED!');
        process.exit(0);
    } else {
        console.log('❌ Some import tests failed.');
        process.exit(1);
    }
}

runImportTests().catch(error => {
    console.error('Import test runner failed:', error);
    process.exit(1);
});