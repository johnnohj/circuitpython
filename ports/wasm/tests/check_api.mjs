#!/usr/bin/env node
import { loadCircuitPython } from '../build-standard/circuitpython.mjs';

const ctpy = await loadCircuitPython({ stdout: () => {}, stderr: () => {} });

console.log('Available methods on ctpy object:');
console.log('================================\n');

const methods = Object.keys(ctpy).sort();
methods.forEach(key => {
    const type = typeof ctpy[key];
    const prefix = key.startsWith('_') ? '  (internal) ' : '  ';
    console.log(`${prefix}${key}: ${type}`);
});

console.log('\n\nChecking specific methods:');
console.log(`  runFile: ${typeof ctpy.runFile}`);
console.log(`  runPython: ${typeof ctpy.runPython}`);
console.log(`  runWorkflow: ${typeof ctpy.runWorkflow}`);
