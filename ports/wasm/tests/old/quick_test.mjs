#!/usr/bin/env node
import { loadCircuitPython } from '../../build-standard/circuitpython.mjs';

console.log('Testing loadCircuitPython API...');

try {
    const ctpy = await loadCircuitPython({
        stdout: () => {},
        stderr: () => {}
    });

    console.log('\n✅ loadCircuitPython() returned successfully');
    console.log('\nChecking key functions:');
    console.log(`  runFile: ${typeof ctpy.runFile}`);
    console.log(`  runPython: ${typeof ctpy.runPython}`);
    console.log(`  FS: ${typeof ctpy.FS}`);
    console.log(`  _virtual_gpio_get_output_value: ${typeof ctpy._virtual_gpio_get_output_value}`);

    if (typeof ctpy.runFile === 'function') {
        console.log('\n✅ runFile API is working!');
    } else {
        console.log('\n❌ runFile is missing!');
    }
} catch (error) {
    console.error('\n❌ Error:', error.message);
    process.exit(1);
}
