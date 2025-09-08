#!/usr/bin/env node

// Test what functions are available in MicroPython module

import _createMicroPythonModule from './build-standard/micropython.mjs';

async function testMicroPythonFunctions() {
    console.log('Loading MicroPython...');
    
    const Module = await _createMicroPythonModule({
        stdout: (data) => process.stdout.write(data),
        linebuffer: false,
    });
    
    console.log('Available functions:');
    const functions = Object.keys(Module).filter(key => typeof Module[key] === 'function');
    functions.forEach(func => {
        console.log(`- ${func}`);
    });
    
    console.log('\nAvailable properties starting with _mp:');
    Object.keys(Module).filter(key => key.startsWith('_mp')).forEach(prop => {
        console.log(`- ${prop}: ${typeof Module[prop]}`);
    });
}

testMicroPythonFunctions().catch(console.error);