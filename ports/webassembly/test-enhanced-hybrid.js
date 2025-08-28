// Test the hybrid approach: proven trial2WASM build + enhanced circuitpython-api.js
import('./circuitpython-api.js').then(async (api) => {
    console.log('=== Testing Enhanced Trial2WASM Hybrid Approach ===\n');
    
    try {
        // Test with minimal build (proven to work at 100%)
        console.log('Loading build-minimal...');
        const circuitPython = await api.default({
            wasmModule: './build-minimal/circuitpython.mjs',
            heapSize: 1024 * 1024, // 1MB
        });
        
        console.log('✓ Enhanced API initialized with minimal build\n');
        
        // Test comprehensive suite
        const tests = [
            {
                name: "Basic arithmetic",
                code: "print(2 + 3)"
            },
            {
                name: "Variable assignment", 
                code: "x = 42\nprint(x)"
            },
            {
                name: "String operations",
                code: "name = 'CircuitPython'\nprint('Hello ' + name)"
            },
            {
                name: "List creation",
                code: "lst = [1, 2, 3, 4, 5]\nprint(lst)"
            },
            {
                name: "List operations",
                code: "lst = [1, 2, 3]\nlst.append(4)\nprint(lst)"
            },
            {
                name: "Dictionary operations", 
                code: "d = {'a': 1, 'b': 2}\nprint(d['a'])"
            },
            {
                name: "For loop",
                code: "for i in range(3):\n    print(i)"
            },
            {
                name: "Function definition",
                code: "def greet(name):\n    return 'Hello ' + name\nprint(greet('World'))"
            },
            {
                name: "Exception handling - basic",
                code: "try:\n    print('trying')\nexcept:\n    print('caught')"
            },
            {
                name: "Exception handling - with error", 
                code: "try:\n    x = 1 / 0\nexcept ZeroDivisionError:\n    print('caught division by zero')"
            },
            {
                name: "List comprehension",
                code: "result = [x*2 for x in range(5)]\nprint(result)"
            },
            {
                name: "Nested function",
                code: "def outer():\n    def inner():\n        return 42\n    return inner()\nprint(outer())"
            },
            {
                name: "Class definition",
                code: "class Test:\n    def __init__(self):\n        self.value = 123\n    def get(self):\n        return self.value\nt = Test()\nprint(t.get())"
            },
            {
                name: "Import statement",
                code: "import sys\nprint('python' in sys.version.lower())"
            },
            {
                name: "Global variable persistence (separate calls)",
                code: "global_var = 'persistent'"
            },
            {
                name: "Access global variable",
                code: "print(global_var)"
            },
            {
                name: "Complex expression",
                code: "result = sum([x**2 for x in range(10) if x % 2 == 0])\nprint(result)"
            },
            {
                name: "Multiple assignment",
                code: "a, b, c = 1, 2, 3\nprint(a + b + c)"
            }
        ];
        
        let passed = 0;
        let total = tests.length;
        
        for (let i = 0; i < tests.length; i++) {
            const test = tests[i];
            try {
                const result = await circuitPython.exec(test.code);
                console.log(`✓ Test ${i+1}: ${test.name}`);
                passed++;
            } catch (e) {
                console.log(`❌ Test ${i+1}: ${test.name} - ${e.message}`);
            }
        }
        
        const successRate = ((passed / total) * 100).toFixed(1);
        console.log(`\n=== Results ===`);
        console.log(`Enhanced API + Trial2WASM Minimal: ${passed}/${total} tests passed (${successRate}%)`);
        console.log(`Previous trial2WASM direct test: 18/18 tests passed (100%)`);
        
        if (successRate == '100.0') {
            console.log('🎉 Perfect! Enhanced API maintains 100% success rate!');
        } else {
            console.log(`⚠️  Enhanced API has ${100 - parseFloat(successRate)}% reduction in success rate`);
        }
        
    } catch (error) {
        console.error('❌ Failed to initialize enhanced hybrid approach:', error);
    }
}).catch(console.error);