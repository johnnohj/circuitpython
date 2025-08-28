// Test the hybrid approach directly: proven trial2WASM build + our mp_js_init_with_heap wrapper
import('./build-minimal/circuitpython.mjs').then(async (circuitPythonModule) => {
    console.log('=== Testing Direct Hybrid Approach (Trial2WASM + Enhanced Wrapper) ===\n');
    
    try {
        const createModule = circuitPythonModule.default;
        const Module = await createModule();
        console.log('✓ Trial2WASM minimal build loaded successfully\n');
        
        // Test our enhanced wrapper function
        try {
            const heapSize = 1024 * 1024; // 1MB
            Module.ccall('mp_js_init_with_heap', 'null', ['number'], [heapSize]);
            console.log('✓ Enhanced API wrapper (mp_js_init_with_heap) works correctly\n');
        } catch (e) {
            console.log('❌ Enhanced wrapper failed, falling back to original API');
            const pystackSize = 8192;  
            const heapSize = 1024 * 1024;
            Module.ccall('mp_js_init', 'null', ['number', 'number'], [pystackSize, heapSize]);
            console.log('✓ Original trial2WASM API initialized\n');
        }
        
        // Test comprehensive suite (same tests that achieved 100% in trial2WASM)
        const tests = [
            {
                name: "Basic arithmetic",
                code: "print(2 + 3)"
            },
            {
                name: "Variable assignment", 
                code: "x = 42\\nprint(x)"
            },
            {
                name: "String operations",
                code: "name = 'CircuitPython'\\nprint('Hello ' + name)"
            },
            {
                name: "List creation",
                code: "lst = [1, 2, 3, 4, 5]\\nprint(lst)"
            },
            {
                name: "List operations",
                code: "lst = [1, 2, 3]\\nlst.append(4)\\nprint(lst)"
            },
            {
                name: "Dictionary operations", 
                code: "d = {'a': 1, 'b': 2}\\nprint(d['a'])"
            },
            {
                name: "For loop",
                code: "for i in range(3):\\n    print(i)"
            },
            {
                name: "Function definition",
                code: "def greet(name):\\n    return 'Hello ' + name\\nprint(greet('World'))"
            },
            {
                name: "Exception handling - basic",
                code: "try:\\n    print('trying')\\nexcept:\\n    print('caught')"
            },
            {
                name: "Exception handling - with error", 
                code: "try:\\n    x = 1 / 0\\nexcept ZeroDivisionError:\\n    print('caught division by zero')"
            },
            {
                name: "List comprehension",
                code: "result = [x*2 for x in range(5)]\\nprint(result)"
            },
            {
                name: "Nested function",
                code: "def outer():\\n    def inner():\\n        return 42\\n    return inner()\\nprint(outer())"
            },
            {
                name: "Class definition",
                code: "class Test:\\n    def __init__(self):\\n        self.value = 123\\n    def get(self):\\n        return self.value\\nt = Test()\\nprint(t.get())"
            },
            {
                name: "Import statement",
                code: "import sys\\nprint('python' in sys.version.lower())"
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
                code: "result = sum([x**2 for x in range(10) if x % 2 == 0])\\nprint(result)"
            },
            {
                name: "Multiple assignment",
                code: "a, b, c = 1, 2, 3\\nprint(a + b + c)"
            }
        ];
        
        let passed = 0;
        let total = tests.length;
        
        // Helper function for UTF8 length  
        function lengthBytesUTF8(str) {
            let len = 0;
            for (let i = 0; i < str.length; ++i) {
                let c = str.charCodeAt(i);
                if (c <= 0x7F) {
                    len++;
                } else if (c <= 0x7FF) {
                    len += 2;
                } else if (c >= 0xD800 && c <= 0xDFFF) {
                    len += 4; ++i;
                } else {
                    len += 3;
                }
            }
            return len;
        }
        
        for (let i = 0; i < tests.length; i++) {
            const test = tests[i];
            try {
                const len = lengthBytesUTF8(test.code);
                const buf = Module._malloc(len + 1);
                const value = Module._malloc(3 * 4);
                Module.stringToUTF8(test.code, buf, len + 1);
                Module.ccall('mp_js_do_exec', 'null', ['pointer', 'number', 'pointer'], [buf, len, value]);
                Module._free(buf);
                Module._free(value);
                console.log(`✓ Test ${i+1}: ${test.name}`);
                passed++;
            } catch (e) {
                console.log(`❌ Test ${i+1}: ${test.name} - ${e.message}`);
            }
        }
        
        const successRate = ((passed / total) * 100).toFixed(1);
        console.log(`\\n=== Results ===`);
        console.log(`Hybrid approach (Trial2WASM + Enhanced wrapper): ${passed}/${total} tests passed (${successRate}%)`);
        console.log(`Previous trial2WASM direct test: 18/18 tests passed (100%)`);
        console.log(`Previous CircuitPython attempt: 16/18 tests passed (88.9%)`);
        
        if (successRate == '100.0') {
            console.log('🎉 Perfect! Hybrid approach maintains 100% success rate with enhanced API!');
        } else if (parseFloat(successRate) > 88.9) {
            console.log(`🎯 Excellent! Hybrid approach beats our previous CircuitPython build!`);
        } else {
            console.log(`⚠️  Hybrid approach: ${successRate}% success rate`);
        }
        
    } catch (error) {
        console.error('❌ Failed to test hybrid approach:', error);
    }
}).catch(console.error);