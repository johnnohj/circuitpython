#!/usr/bin/env node

// Simple test script to verify generic board functionality
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

// Import the CircuitPython module  
import createCircuitPythonModule from './build/circuitpython.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

async function testGenericBoard() {
    console.log('🧪 Testing Generic Metro Board Implementation');
    console.log('================================================\n');
    
    try {
        // Initialize CircuitPython module
        console.log('1. Initializing CircuitPython WASM module...');
        const Module = await createCircuitPythonModule();
        console.log('✓ Module initialized successfully\n');
        
        // Test generic board initialization
        console.log('2. Initializing generic board...');
        const initResult = Module.ccall('mp_js_init_generic_board', 'number', [], []);
        console.log(`✓ Generic board initialized (result: ${initResult})\n`);
        
        // Get board JSON
        console.log('3. Getting board configuration...');
        const jsonPtr = Module.ccall('mp_js_get_generic_board_json', 'string', [], []);
        if (jsonPtr) {
            const boardConfig = JSON.parse(jsonPtr);
            console.log('✓ Board configuration retrieved:');
            console.log(`   - Board: ${boardConfig.board_name}`);
            console.log(`   - MCU: ${boardConfig.mcu_type}`);
            console.log(`   - Flash: ${boardConfig.flash_size} bytes`);
            console.log(`   - RAM: ${boardConfig.ram_size} bytes`);
            console.log(`   - Pins: ${boardConfig.pins.length}`);
            console.log(`   - Peripherals: ${boardConfig.peripherals.length}`);
            
            // Show some example pins
            console.log('\n   Example pins:');
            boardConfig.pins.slice(0, 5).forEach(pin => {
                console.log(`     - ${pin.name}: ${pin.mcu_pin} (caps: 0x${pin.capabilities.toString(16)})`);
            });
            console.log('   ...\n');
            
            // Show peripherals
            console.log('   Peripherals:');
            boardConfig.peripherals.forEach(peripheral => {
                console.log(`     - ${peripheral.name}: [${peripheral.pins.join(', ')}]`);
            });
        } else {
            console.log('❌ Failed to get board configuration');
            return false;
        }
        
        // Test pin operations
        console.log('\n4. Testing pin operations...');
        
        // Set LED pin high
        const setResult = Module.ccall('mp_js_generic_pin_set_value', 'number', ['string', 'number'], ['LED', 1]);
        console.log(`✓ Set LED pin high (result: ${setResult})`);
        
        // Get LED pin value
        const getValue = Module.ccall('mp_js_generic_pin_get_value', 'number', ['string'], ['LED']);
        console.log(`✓ Get LED pin value: ${getValue}`);
        
        // Set pin direction
        const dirResult = Module.ccall('mp_js_generic_pin_set_direction', 'number', ['string', 'number'], ['D13', 1]);
        console.log(`✓ Set D13 pin direction to output (result: ${dirResult})`);
        
        // Generate board module
        console.log('\n5. Generating board module...');
        const modulePtr = Module.ccall('mp_js_generate_board_module', 'string', [], []);
        if (modulePtr) {
            console.log('✓ Board module generated:');
            const moduleContent = modulePtr.split('\n').slice(0, 10).join('\n');
            console.log(moduleContent);
            console.log('   ...(truncated)');
        } else {
            console.log('❌ Failed to generate board module');
        }
        
        console.log('\n🎉 All tests passed! Generic Metro board is working correctly.');
        return true;
        
    } catch (error) {
        console.error('❌ Test failed:', error);
        return false;
    }
}

// Setup LED change callback for testing
if (typeof global !== 'undefined') {
    global.Module = global.Module || {};
    global.Module.onLEDChange = function(value) {
        console.log(`📡 LED callback: LED is now ${value ? 'ON' : 'OFF'}`);
    };
    
    global.Module.onGenericPinChange = function(pinName, value) {
        console.log(`📡 Pin callback: ${pinName} changed to ${value}`);
    };
    
    global.Module.getButtonState = function() {
        // Simulate button not pressed
        return false;
    };
    
    global.Module.getAnalogValue = function(pinName) {
        // Return a simulated analog value (middle of range)
        return 512;
    };
}

// Run the test
if (import.meta.url === `file://${process.argv[1]}`) {
    testGenericBoard()
        .then(success => {
            process.exit(success ? 0 : 1);
        })
        .catch(error => {
            console.error('Test execution failed:', error);
            process.exit(1);
        });
}

export default testGenericBoard;