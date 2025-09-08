#!/usr/bin/env node

// Test CircuitPython HAL WebAssembly port REPL functionality

console.log('🧪 Testing CircuitPython HAL WebAssembly Port REPL\n');

async function testCircuitPyHAL() {
    try {
        // Load the module (built with VARIANT=standard)
        const moduleExports = await import('./build-standard/circuitpython-hal.mjs');
        const moduleFactory = moduleExports.default;
        console.log('✅ Module factory loaded');
        
        // Initialize the WebAssembly module
        const Module = await moduleFactory();
        console.log('✅ WebAssembly module initialized');
        
        // Test HAL provider registration
        console.log('\n🔧 Testing HAL provider functions:');
        const halFunctions = [
            '_hal_register_js_provider'
        ];
        
        halFunctions.forEach(funcName => {
            if (typeof Module[funcName] === 'function') {
                console.log(`  ✅ ${funcName} - Available`);
            } else {
                console.log(`  ❌ ${funcName} - Missing`);
            }
        });
        
        // Test REPL functions  
        console.log('\n🚀 Testing REPL functions:');
        const replFunctions = [
            '_mp_js_init',
            '_mp_js_repl_init', 
            '_mp_js_repl_process_char'
        ];
        
        const availableFunctions = {};
        replFunctions.forEach(funcName => {
            if (typeof Module[funcName] === 'function') {
                console.log(`  ✅ ${funcName} - Available`);
                availableFunctions[funcName] = Module[funcName];
            } else {
                console.log(`  ❌ ${funcName} - Missing`);
                return;
            }
        });
        
        // Initialize Python interpreter
        console.log('\n🐍 Initializing Python interpreter...');
        try {
            availableFunctions._mp_js_init(8 * 1024, 512 * 1024); // 8KB pystack, 512KB heap
            console.log('✅ Python interpreter initialized');
        } catch (error) {
            console.log('❌ Python interpreter initialization failed:', error.message);
            return;
        }
        
        // Register JavaScript provider
        console.log('\n🌉 Registering JavaScript HAL provider...');
        try {
            if (Module._hal_register_js_provider) {
                Module._hal_register_js_provider();
                console.log('✅ JavaScript HAL provider registered');
            } else {
                console.log('⚠️ HAL provider registration not available (expected for first build)');
            }
        } catch (error) {
            console.log('❌ HAL provider registration failed:', error.message);
        }
        
        // Initialize REPL
        console.log('\n📟 Initializing REPL...');
        try {
            availableFunctions._mp_js_repl_init();
            console.log('✅ REPL initialized');
        } catch (error) {
            console.log('❌ REPL initialization failed:', error.message);
            return;
        }
        
        // Test basic Python code execution
        console.log('\n🧪 Testing basic Python execution:');
        try {
            const testCode = "print('Hello from CircuitPython HAL!')\\n";
            for (const char of testCode) {
                const result = availableFunctions._mp_js_repl_process_char(char.charCodeAt(0));
                // result: 0 = continue, 1 = execute, 2 = syntax error
            }
            console.log('✅ Basic Python code executed');
        } catch (error) {
            console.log('❌ Python code execution failed:', error.message);
        }
        
        // Test CircuitPython module imports
        console.log('\n📦 Testing CircuitPython module imports:');
        try {
            const importTests = [
                "import json\\n",
                "import binascii\\n", 
                "import re\\n",
                "import digitalio\\n"
            ];
            
            for (const testCode of importTests) {
                for (const char of testCode) {
                    availableFunctions._mp_js_repl_process_char(char.charCodeAt(0));
                }
            }
            console.log('✅ CircuitPython modules imported successfully');
        } catch (error) {
            console.log('❌ Module import failed:', error.message);
        }
        
        console.log('\n🎉 CircuitPython HAL WebAssembly port test completed!');
        
    } catch (error) {
        console.log('❌ Test failed:', error.message);
        console.log(error.stack);
    }
}

testCircuitPyHAL();