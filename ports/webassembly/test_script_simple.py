print("=== Simple CircuitPython Script ===")
print("Testing basic Python functionality...")

# Variables and math
x = 10
y = 5
print(f"Addition: {x} + {y} = {x + y}")
print(f"Multiplication: {x} * {y} = {x * y}")

# String operations
name = "CircuitPython"
version = "10.0.0"
print(f"Running {name} version {version}")

# Simple list operations
numbers = [1, 2, 3, 4, 5]
print(f"Numbers: {numbers}")
print(f"First number: {numbers[0]}")
print(f"Last number: {numbers[-1]}")

# Import and use modules
import sys
print(f"Python version: {sys.version}")
print(f"Platform: {sys.platform}")

print("=== Script completed! ===")