#!/usr/bin/env node

/**
 * Phase 1 Testing Script
 * Tests the critical fixes implemented in Phase 1
 */

console.log('=== Phase 1 Fix Testing ===\n');

async function testBasicExecution() {
    console.log('Test 1: Basic arithmetic execution...');
    
    try {
        const CircuitPython = await import('./build-standard/circuitpython.mjs');
        
        const mp = await CircuitPython.default({
            print: (text) => console.log('[OUTPUT]', text),
            printErr: (text) => console.error('[ERROR]', text),
            stdout: (charCode) => process.stdout.write(String.fromCharCode(charCode)),
            stderr: (charCode) => process.stderr.write('[STDERR]' + String.fromCharCode(charCode))
        });
        
        // Initialize with our new safer initialization sequence
        mp._mp_js_init_with_heap(8 * 1024 * 1024);
        
        console.log('✅ Initialization successful');
        
        // Test basic arithmetic without imports
        const out = new Uint32Array(3);
        const code = '2 + 2';
        const codePtr = mp.allocateUTF8(code);
        
        console.log('Testing basic arithmetic:', code);
        mp._mp_js_do_exec(codePtr, code.length, out.byteOffset);
        mp._free(codePtr);
        
        console.log('✅ Basic execution test passed');
        return true;
        
    } catch (error) {
        console.error('❌ Basic execution test failed:', error.message);
        return false;
    }
}

async function testREPLInitialization() {
    console.log('\nTest 2: REPL initialization...');
    
    try {
        const CircuitPython = await import('./build-standard/circuitpython.mjs');
        
        const mp = await CircuitPython.default({
            print: (text) => console.log('[REPL]', text),
            printErr: (text) => console.error('[REPL-ERROR]', text)
        });
        
        mp._mp_js_init_with_heap(8 * 1024 * 1024);
        
        // Test REPL initialization
        mp._mp_js_repl_init();
        console.log('✅ REPL initialization successful');
        
        return true;
        
    } catch (error) {
        console.error('❌ REPL initialization test failed:', error.message);
        return false;
    }
}

async function testProxyInitialization() {
    console.log('\nTest 3: Proxy system initialization...');
    
    try {
        const CircuitPython = await import('./build-standard/circuitpython.mjs');
        
        const mp = await CircuitPython.default({
            print: (text) => console.log('[PROXY]', text),
            printErr: (text) => console.error('[PROXY-ERROR]', text)
        });
        
        mp._mp_js_init_with_heap(8 * 1024 * 1024);
        
        // Test if post-init function exists
        if (mp._mp_js_post_init) {
            mp._mp_js_post_init();
            console.log('✅ Post-initialization function available');
        }
        
        // Test if proxy status check exists
        if (mp._proxy_c_is_initialized) {
            const isInit = mp._proxy_c_is_initialized();
            console.log('✅ Proxy status check available, initialized:', isInit);
        }
        
        return true;
        
    } catch (error) {
        console.error('❌ Proxy initialization test failed:', error.message);
        return false;
    }
}

async function testExportedFunctions() {
    console.log('\nTest 4: Exported functions availability...');
    
    try {
        const CircuitPython = await import('./build-standard/circuitpython.mjs');
        const mp = await CircuitPython.default();
        
        const requiredFunctions = [
            '_mp_js_init_with_heap',
            '_mp_js_post_init',
            '_mp_js_repl_init',
            '_proxy_c_is_initialized',
            'allocateUTF8',
            'UTF8ToString'
        ];
        
        const missingFunctions = [];
        
        for (const func of requiredFunctions) {
            if (typeof mp[func] === 'undefined') {
                missingFunctions.push(func);
            }
        }
        
        if (missingFunctions.length === 0) {
            console.log('✅ All required functions exported');
            return true;
        } else {
            console.error('❌ Missing functions:', missingFunctions);
            return false;
        }
        
    } catch (error) {
        console.error('❌ Export test failed:', error.message);
        return false;
    }
}

async function runAllTests() {
    console.log('Testing Phase 1 critical fixes...\n');
    
    const results = [];
    
    results.push(await testBasicExecution());
    results.push(await testREPLInitialization());  
    results.push(await testProxyInitialization());
    results.push(await testExportedFunctions());
    
    const passed = results.filter(r => r).length;
    const total = results.length;
    
    console.log('\n=== Test Results ===');
    console.log(`Passed: ${passed}/${total}`);
    
    if (passed === total) {
        console.log('🎉 All Phase 1 tests passed!');
        process.exit(0);
    } else {
        console.log('❌ Some tests failed. Phase 1 needs more work.');
        process.exit(1);
    }
}

runAllTests().catch(error => {
    console.error('Test runner failed:', error);
    process.exit(1);
});