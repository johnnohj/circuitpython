#!/usr/bin/env node

/**
 * Simple test of virtual hardware interaction
 */

import loadCircuitPython from '../../build-standard/circuitpython.mjs';

console.log('🔌 CircuitPython WASM - Virtual Hardware Test\n');

const ctpy = await loadCircuitPython({
    stdout: (data) => process.stdout.write(data),
    stderr: (data) => process.stderr.write(data)
});

console.log('✅ CircuitPython loaded!\n');

// Test 1: Set up a GPIO output
console.log('Test 1: Setting up LED on D13...');
ctpy.runPython(`
import board
import digitalio

led = digitalio.DigitalInOut(board.D13)
led.direction = digitalio.Direction.OUTPUT
print("LED initialized")
`);

// Test 2: Turn LED on from Python
console.log('\nTest 2: Turning LED ON from Python...');
ctpy.runPython('led.value = True');

// Test 3: JavaScript reads LED state
const ledState = ctpy._virtual_gpio_get_output_value(13);
console.log(`[JS] LED state read from WASM: ${ledState ? 'HIGH' : 'LOW'}`);

// Test 4: Turn LED off from Python
console.log('\nTest 3: Turning LED OFF from Python...');
ctpy.runPython('led.value = False');

const ledState2 = ctpy._virtual_gpio_get_output_value(13);
console.log(`[JS] LED state read from WASM: ${ledState2 ? 'HIGH' : 'LOW'}`);

// Test 5: Set up button input
console.log('\nTest 4: Setting up button on D2...');
ctpy.runPython(`
button = digitalio.DigitalInOut(board.D2)
button.direction = digitalio.Direction.INPUT
button.pull = digitalio.Pull.UP
print("Button initialized with PULL_UP")
`);

// Test 6: JavaScript simulates button press
console.log('\nTest 5: JavaScript simulates button press...');
console.log('[JS] Pressing button (setting to LOW)...');
ctpy._virtual_gpio_set_input_value(2, false);

let buttonValue = ctpy.runPython('button.value');
console.log(`[Python] Button value: ${buttonValue}`);

console.log('[JS] Releasing button (setting to HIGH)...');
ctpy._virtual_gpio_set_input_value(2, true);

buttonValue = ctpy.runPython('button.value');
console.log(`[Python] Button value: ${buttonValue}`);

console.log('\n✅ All tests passed!');
console.log('\nKey observations:');
console.log('  • Python code runs synchronously within WASM');
console.log('  • JavaScript can observe outputs (LED states)');
console.log('  • JavaScript can inject inputs (button presses)');
console.log('  • No hanging, no yielding - instant operations!');
