// Comprehensive Python interpreter tests for CircuitPython WebAssembly
import _createCircuitPythonModule from './build-standard/circuitpython.mjs';
import { readFileSync, existsSync } from 'fs';

class TestRunner {
    constructor() {
        this.mp = null;
        this.outputPtr = null;
        this.passed = 0;
        this.failed = 0;
        this.tests = [];
    }

    async initialize() {
        console.log("Initializing CircuitPython WebAssembly interpreter...");
        this.mp = await _createCircuitPythonModule();
        this.mp._mp_js_init_with_heap(256 * 1024); // 256KB heap for comprehensive tests
        this.outputPtr = this.mp._malloc(4);
        console.log("✓ Interpreter initialized\n");
    }

    test(name, code, expectError = false) {
        this.tests.push({ name, code, expectError });
    }

    async runTest(test) {
        try {
            this.mp._mp_js_do_exec(test.code, test.code.length, this.outputPtr);
            if (test.expectError) {
                console.log(`✗ ${test.name} - Expected error but none occurred`);
                this.failed++;
                return false;
            } else {
                console.log(`✓ ${test.name}`);
                this.passed++;
                return true;
            }
        } catch (error) {
            if (test.expectError) {
                console.log(`✓ ${test.name} - Expected error caught: ${error.message}`);
                this.passed++;
                return true;
            } else {
                console.log(`✗ ${test.name} - Unexpected error: ${error.message}`);
                this.failed++;
                return false;
            }
        }
    }

    async runAllTests() {
        console.log("Running comprehensive Python interpreter tests...\n");

        for (const test of this.tests) {
            await this.runTest(test);
        }

        console.log(`\n📊 Test Results: ${this.passed} passed, ${this.failed} failed, ${this.passed + this.failed} total`);

        if (this.failed === 0) {
            console.log("🎉 All comprehensive tests passed!");
        } else {
            console.log(`⚠️ ${this.failed} tests failed`);
        }

        return this.failed === 0;
    }

    cleanup() {
        if (this.outputPtr) {
            this.mp._free(this.outputPtr);
        }
    }
}

async function runComprehensiveTests() {
    const runner = new TestRunner();
    await runner.initialize();

    // ===== BASIC LANGUAGE FEATURES =====
    console.log("=== Basic Language Features ===");

    runner.test("Variables and assignment", "x = 42; y = 'hello'; z = [1,2,3]");
    runner.test("Arithmetic operations", "result = (10 + 5) * 2 - 3 / 3");
    runner.test("String operations", "s = 'Hello' + ' ' + 'World'; s2 = s.upper()");
    runner.test("Boolean operations", "a = True and False; b = not a; c = a or b");
    runner.test("Comparison operations", "x = 5; result = x > 3 and x < 10");

    // ===== DATA STRUCTURES =====
    console.log("\n=== Data Structures ===");

    runner.test("List operations", "lst = [1,2,3]; lst.append(4); lst.extend([5,6]); lst.pop()");
    runner.test("Dictionary operations", "d = {'a': 1, 'b': 2}; d['c'] = 3; d.update({'d': 4})");
    runner.test("Tuple operations", "t = (1, 2, 3); x, y, z = t; result = t[1]");
    runner.test("Set operations", "s = {1, 2, 3}; s.add(4); s.discard(2)");
    runner.test("List slicing", "lst = [1,2,3,4,5]; result = lst[1:4]; result2 = lst[::-1]");

    // ===== CONTROL FLOW =====
    console.log("\n=== Control Flow ===");

    runner.test("If statements", "x = 10; result = 'positive' if x > 0 else 'negative'");
    runner.test("For loops", "result = []; [result.append(i*2) for i in range(5)]");
    runner.test("While loops", "i = 0; result = []; [result.append(i), i := i + 1 for _ in range(3) if i < 3]");
    runner.test("Break and continue", `
result = []
for i in range(10):
    if i == 3:
        continue
    if i == 7:
        break
    result.append(i)`);

    // ===== FUNCTIONS =====
    console.log("\n=== Functions ===");

    runner.test("Function definition", `
def greet(name, greeting='Hello'):
    return f'{greeting}, {name}!'
result = greet('World')
result2 = greet('Python', 'Hi')`);

    runner.test("Lambda functions", "square = lambda x: x*x; result = square(5)");
    runner.test("Nested functions", `
def outer(x):
    def inner(y):
        return x + y
    return inner
add5 = outer(5)
result = add5(10)`);

    runner.test("Generator functions", `
def fibonacci():
    a, b = 0, 1
    while True:
        yield a
        a, b = b, a + b

fib = fibonacci()
result = [next(fib) for _ in range(5)]`);

    // ===== CLASSES AND OBJECTS =====
    console.log("\n=== Classes and Objects ===");

    runner.test("Class definition", `
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def distance_from_origin(self):
        return (self.x**2 + self.y**2)**0.5

p = Point(3, 4)
result = p.distance_from_origin()`);

    runner.test("Class inheritance", `
class Animal:
    def __init__(self, name):
        self.name = name
    def speak(self):
        pass

class Dog(Animal):
    def speak(self):
        return f'{self.name} says Woof!'

dog = Dog('Rex')
result = dog.speak()`);

    // ===== EXCEPTION HANDLING =====
    console.log("\n=== Exception Handling ===");

    runner.test("Try/except basic", `
try:
    result = 10 / 2
except ZeroDivisionError:
    result = 'error'`);

    runner.test("Multiple except clauses", `
try:
    lst = [1, 2, 3]
    result = lst[1]  # This should work
except IndexError:
    result = 'index_error'
except TypeError:
    result = 'type_error'`);

    runner.test("Finally clause", `
result = []
try:
    result.append('try')
    x = 1 / 1
except:
    result.append('except')
finally:
    result.append('finally')`);

    runner.test("Exception with division by zero", "x = 1 / 0", true);  // Expect error

    // ===== COMPREHENSIONS =====
    console.log("\n=== Comprehensions ===");

    runner.test("List comprehension", "result = [x*2 for x in range(5) if x % 2 == 0]");
    runner.test("Dict comprehension", "result = {x: x**2 for x in range(5)}");
    runner.test("Set comprehension", "result = {x % 3 for x in range(10)}");
    runner.test("Nested comprehension", "matrix = [[i+j for j in range(3)] for i in range(3)]");

    // ===== BUILT-IN FUNCTIONS =====
    console.log("\n=== Built-in Functions ===");

    runner.test("Map function", "result = list(map(lambda x: x*2, [1, 2, 3, 4]))");
    runner.test("Filter function", "result = list(filter(lambda x: x % 2 == 0, range(10)))");
    runner.test("Reduce function", "from functools import reduce; result = reduce(lambda x, y: x+y, [1,2,3,4], 0)");
    runner.test("Zip function", "result = list(zip([1,2,3], ['a','b','c']))");
    runner.test("Enumerate function", "result = list(enumerate(['a','b','c']))");

    // ===== MODULE SYSTEM =====
    console.log("\n=== Module System ===");

    runner.test("Import sys", "import sys; result = hasattr(sys, 'version')");
    runner.test("Import math", "import math; result = math.pi");
    runner.test("From import", "from math import sqrt; result = sqrt(16)");
    runner.test("Import json", "import json; result = json.dumps({'key': 'value'})");
    runner.test("Import collections", "from collections import defaultdict; d = defaultdict(int); d['key'] += 1");

    // ===== MEMORY AND PERFORMANCE =====
    console.log("\n=== Memory and Performance ===");

    runner.test("Large list creation", "big_list = list(range(1000)); result = len(big_list)");
    runner.test("Nested data structures", `
data = {
    'users': [
        {'name': 'Alice', 'scores': [95, 87, 92]},
        {'name': 'Bob', 'scores': [78, 85, 90]}
    ]
}
result = sum(data['users'][0]['scores'])`);

    runner.test("String formatting", `
name = 'Python'
version = '3.8'
result = f'Welcome to {name} {version}!'
result2 = 'Welcome to {} {}!'.format(name, version)
result3 = 'Welcome to %s %s!' % (name, version)`);

    // ===== ADVANCED FEATURES =====
    console.log("\n=== Advanced Features ===");

    runner.test("Decorators", `
def my_decorator(func):
    def wrapper(*args, **kwargs):
        result = func(*args, **kwargs)
        return result * 2
    return wrapper

@my_decorator
def multiply(x, y):
    return x * y

result = multiply(3, 4)`);

    runner.test("Context managers", `
class MyContext:
    def __enter__(self):
        return 'context_value'
    def __exit__(self, exc_type, exc_val, exc_tb):
        return False

with MyContext() as ctx:
    result = ctx`);

    // Run all tests
    const success = await runner.runAllTests();
    runner.cleanup();

    return success;
}

// Run comprehensive tests
runComprehensiveTests().then(success => {
    console.log(success ? "\n🎉 All comprehensive interpreter tests passed!" : "\n❌ Some tests failed");
    process.exit(success ? 0 : 1);
}).catch(error => {
    console.error("Test runner failed:", error);
    process.exit(1);
});
