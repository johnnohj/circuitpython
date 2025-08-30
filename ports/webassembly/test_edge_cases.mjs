// Edge case and error handling tests for CircuitPython WebAssembly
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testEdgeCases() {
    console.log("Testing edge cases and error handling...\n");
    
    const mp = await _createCircuitPythonModule();
    mp._mp_js_init_with_heap(128 * 1024);
    const outputPtr = mp._malloc(4);

    const tests = [
        {
            name: "Division by zero handling",
            code: "try:\n  result = 1 / 0\n  print('No error - result:', result)\nexcept ZeroDivisionError as e:\n  print('Caught ZeroDivisionError:', str(e))\nexcept Exception as e:\n  print('Caught other exception:', type(e).__name__, str(e))"
        },
        {
            name: "Invalid syntax detection", 
            code: "try:\n  exec('def bad_syntax(')\nexcept SyntaxError as e:\n  print('Caught SyntaxError:', str(e))\nexcept Exception as e:\n  print('Other error:', type(e).__name__)"
        },
        {
            name: "Index out of bounds",
            code: "try:\n  lst = [1, 2, 3]\n  result = lst[10]\nexcept IndexError as e:\n  print('Caught IndexError:', str(e))\nexcept Exception as e:\n  print('Other error:', type(e).__name__)"
        },
        {
            name: "Key error in dictionary",
            code: "try:\n  d = {'a': 1}\n  result = d['missing_key']\nexcept KeyError as e:\n  print('Caught KeyError:', str(e))\nexcept Exception as e:\n  print('Other error:', type(e).__name__)"
        },
        {
            name: "Type error detection",
            code: "try:\n  result = 'string' + 5\nexcept TypeError as e:\n  print('Caught TypeError:', str(e))\nexcept Exception as e:\n  print('Other error:', type(e).__name__)"
        },
        {
            name: "Large integer handling",
            code: "big_num = 2**100; print('Large number:', big_num); print('Type:', type(big_num).__name__)"
        },
        {
            name: "Unicode string handling", 
            code: "unicode_str = 'Hello 🌍 World! α β γ'; print('Unicode length:', len(unicode_str)); print('Unicode string:', unicode_str)"
        },
        {
            name: "Recursive function depth",
            code: `
import sys
print('Recursion limit:', sys.getrecursionlimit() if hasattr(sys, 'getrecursionlimit') else 'unknown')

def factorial(n, depth=0):
    if depth > 50:  # Prevent excessive recursion in test
        return 1
    return 1 if n <= 1 else n * factorial(n-1, depth+1)

result = factorial(10)
print('Factorial result:', result)`
        },
        {
            name: "Memory allocation patterns",
            code: `
# Test various allocation patterns
lists = []
for i in range(100):
    lists.append([j for j in range(i % 10)])

total_elements = sum(len(lst) for lst in lists)
print('Created lists with total elements:', total_elements)

# Cleanup
lists.clear()
print('Memory test completed')`
        },
        {
            name: "Float precision and special values",
            code: `
import math
print('Pi:', math.pi)
print('E:', math.e)
print('Infinity:', float('inf'))
print('NaN:', float('nan'))
print('Is NaN:', math.isnan(float('nan')))
print('Is infinite:', math.isinf(float('inf')))`
        },
        {
            name: "Complex number support",
            code: `
c1 = 3 + 4j
c2 = 1 + 2j
result = c1 * c2
print('Complex multiplication:', result)
print('Real part:', result.real)
print('Imaginary part:', result.imag)
print('Absolute value:', abs(c1))`
        },
        {
            name: "Bytes and bytearray handling",
            code: `
data = b'Hello World'
print('Bytes length:', len(data))
print('First byte:', data[0])

ba = bytearray(b'test')
ba.append(33)  # Add '!' 
print('Bytearray:', ba)
print('Decoded:', ba.decode('utf-8'))`
        }
    ];

    let passed = 0;
    let total = tests.length;

    for (const test of tests) {
        try {
            console.log(`--- ${test.name} ---`);
            mp._mp_js_do_exec(test.code, test.code.length, outputPtr);
            console.log("✓ Test completed\n");
            passed++;
        } catch (error) {
            console.log(`✗ Test failed with error: ${error.message}\n`);
        }
    }

    mp._free(outputPtr);

    console.log(`📊 Edge Case Test Results: ${passed}/${total} tests passed`);
    
    if (passed === total) {
        console.log("🎉 All edge case tests passed!");
    } else {
        console.log(`⚠️ ${total - passed} edge case tests had issues`);
    }

    return passed;
}

testEdgeCases();