// Phase 2 Test - CircuitPython modules
import { loadMicroPython } from '../../build-standard/circuitpython.mjs';

const mp = await loadMicroPython();

console.log('\n=== Phase 2: CircuitPython Modules Test ===\n');

// Test 1: CircuitPython time module
console.log('Test 1: CircuitPython time module');
try {
    mp.runPython(`
import time
start = time.monotonic()
print(f"time.monotonic() = {start}")
print(f"Waiting 100ms...")
time.sleep(0.1)
end = time.monotonic()
elapsed = end - start
print(f"Elapsed: {elapsed:.3f}s (should be ~0.1s)")
`);
    console.log('✅ time module works!\n');
} catch (e) {
    console.log('❌ time module failed:', e.message, '\n');
}

// Test 2: Check available modules
console.log('Test 2: Available CircuitPython modules');
mp.runPython(`
import sys
print("Available modules:")
for mod in sorted(sys.modules.keys()):
    if not mod.startswith('_'):
        print(f"  - {mod}")
`);

console.log('\n=== Phase 2 Test Complete ===\n');
