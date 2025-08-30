// Analyze division by zero behavior in CircuitPython WebAssembly
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';

async function analyzeDivisionBehavior() {
    console.log("Analyzing division by zero behavior...\n");
    
    const mp = await _createCircuitPythonModule();
    mp._mp_js_init_with_heap(64 * 1024);
    const outputPtr = mp._malloc(4);

    const tests = [
        {
            name: "Integer division by zero",
            code: `
try:
    result = 1 // 0
    print("Integer division result:", result)
    print("Type:", type(result).__name__)
except Exception as e:
    print("Exception:", type(e).__name__, "-", str(e))`
        },
        {
            name: "Float division by zero", 
            code: `
try:
    result = 1.0 / 0.0
    print("Float division result:", result)
    print("Type:", type(result).__name__)
    print("Is infinite:", result == float('inf'))
except Exception as e:
    print("Exception:", type(e).__name__, "-", str(e))`
        },
        {
            name: "Regular division by zero",
            code: `
try:
    result = 1 / 0
    print("Regular division result:", result)
    print("Type:", type(result).__name__)
    if hasattr(result, '__class__'):
        print("Class:", result.__class__.__name__)
    # Check if it's infinity
    import math
    if hasattr(math, 'isinf'):
        print("Is infinite:", math.isinf(result))
    else:
        print("math.isinf not available")
except Exception as e:
    print("Exception caught:", type(e).__name__, "-", str(e))`
        },
        {
            name: "Zero division in different contexts",
            code: `
import math
print("Testing various division scenarios:")

# Test 1: Direct division
try:
    print("1. Direct 1/0:")
    result = 1 / 0
    print("   Result:", result, type(result).__name__)
except Exception as e:
    print("   Exception:", type(e).__name__)

# Test 2: In function
try:
    print("2. In function:")
    def divide_test():
        return 5 / 0
    result = divide_test()
    print("   Result:", result, type(result).__name__)
except Exception as e:
    print("   Exception:", type(e).__name__)

# Test 3: With variables
try:
    print("3. With variables:")
    x, y = 10, 0
    result = x / y
    print("   Result:", result, type(result).__name__)
except Exception as e:
    print("   Exception:", type(e).__name__)

# Test 4: Check Python version behavior expectations
print("4. Python implementation info:")
import sys
print("   Implementation:", sys.implementation.name if hasattr(sys.implementation, 'name') else 'unknown')
print("   Version:", sys.version_info[:2] if hasattr(sys, 'version_info') else 'unknown')
print("   Platform:", sys.platform)`
        },
        {
            name: "Math module infinity handling",
            code: `
import math
print("Math module infinity tests:")
print("  math.inf available:", hasattr(math, 'inf'))
print("  math.isinf available:", hasattr(math, 'isinf'))
print("  math.isfinite available:", hasattr(math, 'isfinite'))

if hasattr(math, 'inf'):
    inf = math.inf
    print("  math.inf:", inf)
    print("  math.inf type:", type(inf).__name__)
    if hasattr(math, 'isinf'):
        print("  math.isinf(math.inf):", math.isinf(inf))

# Test float('inf')
try:
    inf2 = float('inf')
    print("  float('inf'):", inf2)
    print("  float('inf') type:", type(inf2).__name__)
    if hasattr(math, 'isinf'):
        print("  math.isinf(float('inf')):", math.isinf(inf2))
except Exception as e:
    print("  float('inf') failed:", e)`
        }
    ];

    for (const test of tests) {
        try {
            console.log(`--- ${test.name} ---`);
            mp._mp_js_do_exec(test.code, test.code.length, outputPtr);
            console.log("");
        } catch (error) {
            console.log(`Test execution failed: ${error.message}\n`);
        }
    }

    mp._free(outputPtr);
}

analyzeDivisionBehavior();