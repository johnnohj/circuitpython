# Test script to verify sys.path initialization and module imports
# This script will be executed via the CircuitPython CLI

print("=== Testing sys.path initialization ===")
import sys
print("sys.path:", sys.path)
print("len(sys.path):", len(sys.path))

print("\n=== Testing basic module imports ===")
try:
    import math
    print("✓ math imported successfully, math.pi =", math.pi)
except ImportError as e:
    print("✗ math import failed:", e)

try:
    import collections
    print("✓ collections imported successfully")
    d = collections.defaultdict(int)
    d['test'] = 42
    print("  defaultdict test:", dict(d))
except ImportError as e:
    print("✗ collections import failed:", e)

try:
    import json
    print("✓ json imported successfully")
    test_data = {"test": "data"}
    encoded = json.dumps(test_data)
    decoded = json.loads(encoded)
    print("  json round-trip test:", decoded)
except ImportError as e:
    print("✗ json import failed:", e)

print("\n=== Testing frozen module imports ===")
try:
    import _boot
    print("✓ _boot imported successfully")
except ImportError as e:
    print("✗ _boot not available:", e)

print("\n=== Import test completed ===")
print("If no ✗ errors appeared above, sys.path initialization is working correctly!")