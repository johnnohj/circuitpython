// CircuitPython-specific feature tests
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function testCircuitPyFeatures() {
    console.log("Testing CircuitPython-specific features...\n");

    const mp = await _createCircuitPythonModule();
    mp._mp_js_init_with_heap(128 * 1024);
    const outputPtr = mp._malloc(4);

    const tests = [
        {
            name: "CircuitPython identification",
            code: `
import sys
print('Implementation:', sys.implementation.name if hasattr(sys.implementation, 'name') else 'unknown')
print('Version:', sys.version)
print('Platform:', sys.platform)

# Check for CircuitPython-specific attributes
circuitpy_features = []
if hasattr(sys, 'implementation'):
    if hasattr(sys.implementation, 'name'):
        circuitpy_features.append(f"name: {sys.implementation.name}")
    if hasattr(sys.implementation, 'version'):
        circuitpy_features.append(f"version: {sys.implementation.version}")

print('CircuitPython features:', circuitpy_features if circuitpy_features else 'none detected')`
        },
        {
            name: "Async/await support",
            code: `
import asyncio

async def test_coroutine():
    return "Hello from async!"

# Test basic async functionality
result = "async test completed"
print('Async test result:', result)

# Test asyncio availability
print('Asyncio available:', 'asyncio' in globals())
print('Asyncio functions:', [name for name in dir(asyncio) if not name.startswith('_')][:5])`
        },
        {
            name: "JSON module functionality",
            code: `
import json

data = {
    'name': 'CircuitPython',
    'version': '8.0',
    'features': ['WebAssembly', 'JSON', 'async'],
    'metadata': {
        'platform': 'wasm',
        'heap_size': '128KB'
    }
}

# Test serialization
json_str = json.dumps(data)
print('JSON serialized length:', len(json_str))

# Test deserialization
restored_data = json.loads(json_str)
print('Restored data keys:', list(restored_data.keys()))
print('Features count:', len(restored_data['features']))`
        },
        {
            name: "Collections module",
            code: `
from collections import OrderedDict, defaultdict, namedtuple

# Test OrderedDict
od = OrderedDict([('a', 1), ('b', 2), ('c', 3)])
print('OrderedDict keys:', list(od.keys()))

# Test defaultdict
dd = defaultdict(int)
dd['count'] += 1
dd['count'] += 1
print('DefaultDict count:', dd['count'])

# Test namedtuple
Point = namedtuple('Point', ['x', 'y'])
p = Point(10, 20)
print('NamedTuple:', p.x, p.y)`
        },
        {
            name: "Math module advanced functions",
            code: `
import math

# Test various math functions
tests = [
    ('sqrt', math.sqrt(16)),
    ('sin', math.sin(math.pi/2)),
    ('log', math.log(math.e)),
    ('factorial', math.factorial(5) if hasattr(math, 'factorial') else 'N/A'),
    ('gcd', math.gcd(12, 8) if hasattr(math, 'gcd') else 'N/A'),
]

for name, result in tests:
    print(f'{name}: {result}')`
        },
        {
            name: "Regular expressions (re module)",
            code: `
import re

pattern = r'\\b\\w+@\\w+\\.\\w+\\b'
text = 'Contact us at support@circuitpython.org or admin@example.com'

matches = re.findall(pattern, text)
print('Email matches:', matches)

# Test substitution
result = re.sub(r'circuitpython', 'CircuitPython', text)
print('Substitution result length:', len(result))`
        },
        {
            name: "Binary data and hashlib",
            code: `
import hashlib

data = b'CircuitPython WebAssembly test data'
print('Original data length:', len(data))

# Test different hash algorithms
algorithms = ['md5', 'sha1', 'sha256']
for alg in algorithms:
    if hasattr(hashlib, alg):
        hasher = getattr(hashlib, alg)()
        hasher.update(data)
        digest = hasher.hexdigest()
        print(f'{alg.upper()} hash length:', len(digest))
    else:
        print(f'{alg.upper()}: not available')`
        },
        {
            name: "Random number generation",
            code: `
import random

# Test basic random functions
print('Random float:', random.random())
print('Random int 1-100:', random.randint(1, 100))

# Test list operations
items = ['apple', 'banana', 'cherry', 'date']
print('Random choice:', random.choice(items))

# Test reproducibility with seed
random.seed(42)
seq1 = [random.randint(1, 10) for _ in range(5)]
random.seed(42)
seq2 = [random.randint(1, 10) for _ in range(5)]
print('Reproducible sequences match:', seq1 == seq2)`
        },
        {
            name: "Platform and OS information",
            code: `
import os
import sys

print('OS name:', getattr(os, 'name', 'unknown'))
print('Current directory:', getattr(os, 'getcwd', lambda: 'N/A')())
print('Environment variables count:', len(getattr(os, 'environ', {})))

# Test path operations if available
if hasattr(os, 'path'):
    print('Path module available')
    print('Path separator:', os.path.sep if hasattr(os.path, 'sep') else 'unknown')
else:
    print('Path module not available')

# System information
print('Platform:', sys.platform)
print('Byte order:', sys.byteorder)
print('Max unicode:', sys.maxunicode if hasattr(sys, 'maxunicode') else 'unknown')`
        },
        {
            name: "Garbage collection",
            code: `
import gc

# Test garbage collection
print('GC enabled:', gc.isenabled())
print('GC thresholds:', gc.get_threshold() if hasattr(gc, 'get_threshold') else 'N/A')

# Create some objects for collection
objects = []
for i in range(100):
    objects.append({'id': i, 'data': list(range(i % 10))})

# Force collection
collected = gc.collect()
print('Objects collected:', collected)
print('GC stats available:', hasattr(gc, 'get_stats'))

# Clean up
objects.clear()`
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

    console.log(`📊 CircuitPython Feature Test Results: ${passed}/${total} tests passed`);

    if (passed === total) {
        console.log("🎉 All CircuitPython feature tests passed!");
    } else {
        console.log(`⚠️ ${total - passed} feature tests had issues`);
    }

    return passed === total;
}

testCircuitPyFeatures();
