/**
 * Test ulab (scientific computing) functionality
 */

import { loadCircuitPython } from '../build-standard/circuitpython.mjs';

async function main() {
    console.log("CircuitPython WASM ulab Test");
    console.log("============================\n");

    const ctpy = await loadCircuitPython();
    console.log("✓ Module loaded\n");

    // Test 1: Basic ulab import
    console.log("=== TEST 1: Import ulab ===");
    try {
        ctpy.runPython(`
import ulab
print("✓ ulab imported successfully")
print(f"  ulab version: {ulab.__version__ if hasattr(ulab, '__version__') else 'unknown'}")
`);
    } catch (e) {
        console.error("✗ Failed to import ulab:", e.message);
        process.exit(1);
    }

    // Test 2: NumPy arrays
    console.log("\n=== TEST 2: NumPy-like arrays ===");
    ctpy.runPython(`
import ulab.numpy as np

# Create arrays
a = np.array([1, 2, 3, 4, 5])
print(f"Array a: {a}")
print(f"  Shape: {a.shape}")
print(f"  Size: {a.size}")

b = np.array([10, 20, 30, 40, 50])
print(f"Array b: {b}")

# Array operations
c = a + b
print(f"a + b = {c}")

d = a * 2
print(f"a * 2 = {d}")
`);

    // Test 3: Mathematical functions
    console.log("\n=== TEST 3: Mathematical functions ===");
    ctpy.runPython(`
import ulab.numpy as np

# Create array
x = np.array([0, 1, 2, 3, 4])
print(f"x = {x}")

# Math functions
print(f"sum(x) = {np.sum(x)}")
print(f"mean(x) = {np.mean(x)}")
print(f"min(x) = {np.min(x)}")
print(f"max(x) = {np.max(x)}")
`);

    // Test 4: 2D arrays (matrices)
    console.log("\n=== TEST 4: 2D arrays (matrices) ===");
    ctpy.runPython(`
import ulab.numpy as np

# Create 2D array
matrix = np.array([[1, 2, 3], [4, 5, 6]])
print(f"Matrix:\\n{matrix}")
print(f"  Shape: {matrix.shape}")

# Transpose
print(f"Transpose:\\n{np.transpose(matrix)}")
`);

    // Test 5: Linear algebra
    console.log("\n=== TEST 5: Linear algebra ===");
    try {
        ctpy.runPython(`
import ulab.numpy as np

# Dot product
a = np.array([1, 2, 3])
b = np.array([4, 5, 6])
print(f"dot(a, b) = {np.dot(a, b)}")
`);
    } catch (e) {
        console.log("  (Linear algebra functions may vary by ulab version)");
    }

    console.log("\n✓ All ulab tests completed!");

    // Stop virtual clock so process can exit
    ctpy.virtualClock.stopRealtime();
}

main().catch(err => {
    console.error("Test failed:", err);
    process.exit(1);
});
