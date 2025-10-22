// Simple test script for CircuitPython WASM
import { loadMicroPython } from './build-standard/circuitpython.mjs';

const mp = await loadMicroPython();

console.log('\n=== CircuitPython WASM Test ===\n');

// Test 1: Basic print
console.log('Test 1: Basic print');
mp.runPython('print("Hello from CircuitPython WASM!")');

// Test 2: Math
console.log('\nTest 2: Math operations');
mp.runPython('print(f"2 + 2 = {2 + 2}")');
mp.runPython('print(f"10 ** 3 = {10 ** 3}")');

// Test 3: List comprehension
console.log('\nTest 3: List comprehension');
mp.runPython('squares = [x**2 for x in range(5)]');
mp.runPython('print(f"Squares: {squares}")');

// Test 4: Import sys and check platform
console.log('\nTest 4: System info');
mp.runPython('import sys');
mp.runPython('print(f"Platform: {sys.platform}")');
mp.runPython('print(f"Version: {sys.version}")');

// Test 5: Python-to-JS interaction
console.log('\nTest 5: Python accessing JavaScript');
mp.runPython('import js');
mp.runPython('print(f"JS globalThis type: {type(js.globalThis)}")');

console.log('\n=== All tests completed! ===\n');
