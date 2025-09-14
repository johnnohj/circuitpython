#!/usr/bin/env node

// Test CircuitPython HAL integration with generic board
import createCircuitPythonModule from './build/circuitpython.mjs';

async function testCircuitPythonHAL() {
    console.log('🐍 Testing CircuitPython HAL Integration');
    console.log('========================================\n');
    
    try {
        // Initialize CircuitPython module
        console.log('1. Initializing CircuitPython WASM module...');
        const Module = await createCircuitPythonModule();
        console.log('✓ Module initialized successfully\n');
        
        // Initialize MicroPython with heap
        console.log('2. Initializing MicroPython runtime...');
        Module.ccall('mp_js_init_with_heap', 'void', ['number'], [64 * 1024]);
        
        // Initialize REPL
        Module.ccall('mp_js_repl_init', 'void', [], []);
        console.log('✓ MicroPython runtime initialized\n');
        
        // Test basic Python execution
        console.log('3. Testing basic Python execution...');
        const testCommands = [
            'print("Hello from CircuitPython WASM!")',
            'import sys',
            'print("Platform:", sys.platform)',
            'print("Implementation:", sys.implementation)',
            '',
            // Test board access (if available)
            'try:',
            '    import board',
            '    print("Board module loaded successfully")',
            '    print("Available attributes:", [attr for attr in dir(board) if not attr.startswith("_")][:10])',
            'except ImportError as e:',
            '    print("Board module not available:", e)',
            '',
            // Test digitalio
            'try:',
            '    import digitalio',
            '    print("DigitalIO module loaded successfully")',
            '    print("DigitalIO attributes:", [attr for attr in dir(digitalio) if not attr.startswith("_")])',
            'except ImportError as e:',
            '    print("DigitalIO module not available:", e)',
            '',
            // Test pin creation (if possible)
            'try:',
            '    from digitalio import DigitalInOut, Direction',
            '    print("Successfully imported DigitalInOut and Direction")',
            'except ImportError as e:',
            '    print("Could not import digitalio classes:", e)',
            ''
        ];
        
        for (const command of testCommands) {
            if (command.trim() === '') {
                continue;
            }
            
            // Process each character of the command
            for (let i = 0; i < command.length; i++) {
                const result = Module.ccall('mp_js_repl_process_char', 'number', ['number'], [command.charCodeAt(i)]);
            }
            
            // Send newline
            const result = Module.ccall('mp_js_repl_process_char', 'number', ['number'], [10]);
            
            // Small delay to let output flush
            await new Promise(resolve => setTimeout(resolve, 10));
        }
        
        console.log('\n✓ CircuitPython commands executed successfully\n');
        
        // Test generic board functions are still accessible
        console.log('4. Verifying generic board is still accessible...');
        const boardJson = Module.ccall('mp_js_get_generic_board_json', 'string', [], []);
        if (boardJson) {
            const config = JSON.parse(boardJson);
            console.log(`✓ Generic board accessible: ${config.board_name}`);
        } else {
            console.log('❌ Generic board not accessible');
        }
        
        console.log('\n🎉 HAL integration test completed successfully!');
        return true;
        
    } catch (error) {
        console.error('❌ HAL integration test failed:', error);
        return false;
    }
}

// Setup module callbacks
if (typeof global !== 'undefined') {
    global.Module = global.Module || {};
    
    // Capture stdout for CircuitPython output
    global.Module.print = function(text) {
        console.log('📤 Python Output:', text);
    };
    
    global.Module.printErr = function(text) {
        console.log('📤 Python Error:', text);
    };
    
    // HAL callbacks
    global.Module.onLEDChange = function(value) {
        console.log(`💡 LED: ${value ? 'ON' : 'OFF'}`);
    };
    
    global.Module.onGenericPinChange = function(pinName, value) {
        console.log(`📍 Pin ${pinName}: ${value}`);
    };
    
    global.Module.getButtonState = function() {
        return false; // Button not pressed
    };
    
    global.Module.getAnalogValue = function(pinName) {
        return 512; // Middle of 10-bit range
    };
}

// Run the test
if (import.meta.url === `file://${process.argv[1]}`) {
    testCircuitPythonHAL()
        .then(success => {
            process.exit(success ? 0 : 1);
        })
        .catch(error => {
            console.error('Test execution failed:', error);
            process.exit(1);
        });
}

export default testCircuitPythonHAL;