#!/usr/bin/env node

/**
 * Test the virtual hardware JavaScript API
 */

import loadCircuitPython from '../../build-standard/circuitpython.mjs';

console.log('🔌 Testing Virtual Hardware JavaScript API\n');

const ctpy = await loadCircuitPython({
    stdout: (data) => {}, // Suppress output for this test
    stderr: (data) => process.stderr.write(data)
});

console.log('✅ CircuitPython loaded!\n');

// Check which functions are available
console.log('Checking available functions:');
console.log(`  _virtual_gpio_set_input_value: ${typeof ctpy._virtual_gpio_set_input_value}`);
console.log(`  _virtual_gpio_get_output_value: ${typeof ctpy._virtual_gpio_get_output_value}`);
console.log(`  _virtual_gpio_get_direction: ${typeof ctpy._virtual_gpio_get_direction}`);
console.log(`  _virtual_gpio_get_pull: ${typeof ctpy._virtual_gpio_get_pull}`);

if (typeof ctpy._virtual_gpio_get_direction === 'function') {
    console.log('\n✅ Virtual hardware API is accessible!');

    // Test reading a pin's direction (should be input by default)
    const direction = ctpy._virtual_gpio_get_direction(13);
    console.log(`\nPin 13 direction: ${direction} (0=input, 1=output)`);

    console.log('\nNow testing with actual GPIO setup...');
} else {
    console.error('\n❌ Virtual hardware API not found!');
    process.exit(1);
}
