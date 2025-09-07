print("=== Basic CircuitPython Test ===")
print("Testing import capabilities...")

try:
    import jsffi
    print("✓ jsffi imported")
except ImportError as e:
    print("✗ jsffi failed:", e)

try:
    import board  
    print("✓ board imported")
    pin_count = len([attr for attr in dir(board) if attr.startswith('GP')])
    print(f"✓ Found {pin_count} GPIO pins")
except ImportError as e:
    print("✗ board failed:", e)

try:
    import digitalio
    print("✓ digitalio imported")
except ImportError as e:
    print("✗ digitalio failed:", e)

try:
    import analogio
    print("✓ analogio imported")  
except ImportError as e:
    print("✗ analogio failed:", e)

try:
    import busio
    print("✓ busio imported")
except ImportError as e:
    print("✗ busio failed:", e)

print("=== Import Test Complete ===")