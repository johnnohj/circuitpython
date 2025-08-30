#!/usr/bin/env python3

print("=== CircuitPython Script Test ===")
print(f"Running advanced Python features...")

# Test variables and math
x = 42
y = 13
result = x * y + 5
print(f"Math result: {x} * {y} + 5 = {result}")

# Test lists and comprehensions
numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
squares = [n**2 for n in numbers if n % 2 == 0]
print(f"Even squares: {squares}")

# Test dictionaries
person = {
    "name": "CircuitPython User",
    "language": "Python",
    "platform": "WebAssembly"
}
print(f"Person info: {person}")

# Test functions
def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n-1) + fibonacci(n-2)

print("Fibonacci sequence:", [fibonacci(i) for i in range(10)])

# Test imports
import sys
import math
print(f"Platform: {sys.platform}")
print(f"Pi: {math.pi:.4f}")
print(f"Square root of 16: {math.sqrt(16)}")

# Test error handling
try:
    result = 10 / 2
    print(f"Division result: {result}")
except ZeroDivisionError:
    print("Division by zero!")

print("=== Script completed successfully! ===")