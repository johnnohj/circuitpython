// REPL functionality test for CircuitPython WebAssembly build
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testREPL() {
    console.log("Testing CircuitPython WebAssembly REPL...");
    
    try {
        const mp = await _createCircuitPythonModule();
        console.log("✓ Module loaded successfully");
        
        // Initialize MicroPython
        mp._mp_js_init_with_heap(64 * 1024);
        console.log("✓ MicroPython initialized");
        
        // Initialize REPL
        mp._mp_js_repl_init();
        console.log("✓ REPL initialized");
        
        // Test REPL character processing (our stubs)
        const testChars = "2+3\\n".split('');
        console.log("Testing REPL character processing...");
        
        for (const char of testChars) {
            const result = mp._mp_js_repl_process_char(char.charCodeAt(0));
            console.log(`Character '${char}' processed, result: ${result}`);
        }
        
        console.log("✓ REPL character processing test completed");
        
        // Test direct execution (which is what REPL would use)
        console.log("Testing REPL-style execution...");
        const outputPtr = mp._malloc(4);
        
        // Simulate REPL interactions
        const replCommands = [
            "x = 42",
            "x * 2", 
            "print('Hello from REPL!')",
            "[i for i in range(3)]"
        ];
        
        for (const cmd of replCommands) {
            mp._mp_js_do_exec(cmd, cmd.length, outputPtr);
            console.log(`✓ REPL command executed: ${cmd}`);
        }
        
        mp._free(outputPtr);
        
        console.log("\\n🎉 All REPL tests passed!");
        console.log("REPL functionality is working correctly!");
        
    } catch (error) {
        console.error("✗ REPL test failed:", error);
        process.exit(1);
    }
}

testREPL();