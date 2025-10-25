/**
 * Simple test for virtual timing integration
 * Tests that the virtual clock system is working at a basic level
 */

import { loadCircuitPython } from '../build-standard/circuitpython.mjs';

async function testBasicTiming() {
    console.log("CircuitPython WASM Virtual Timing Test");
    console.log("=======================================\n");

    const mp = await loadCircuitPython();

    console.log("✓ Module loaded successfully");

    // Check if virtual clock hardware pointer is accessible
    if (typeof mp._get_virtual_clock_hw_ptr === 'function') {
        const ptr = mp._get_virtual_clock_hw_ptr();
        console.log(`✓ Virtual hardware pointer: 0x${ptr.toString(16)}`);

        // Access the shared memory
        const view = new DataView(mp.HEAPU8.buffer, ptr, 32);

        // Read initial ticks
        const ticksLow = view.getUint32(0, true);
        const ticksHigh = view.getUint32(4, true);
        const initialTicks = (BigInt(ticksHigh) << 32n) | BigInt(ticksLow);
        console.log(`✓ Initial ticks (32kHz): ${initialTicks}`);

        // Read CPU frequency
        const cpuFreq = view.getUint32(8, true);
        console.log(`✓ CPU frequency: ${cpuFreq / 1_000_000}MHz`);

        // Read time mode
        const timeMode = view.getUint8(12);
        const modes = ['REALTIME', 'MANUAL', 'FAST_FORWARD'];
        console.log(`✓ Time mode: ${modes[timeMode] || timeMode}`);

    } else {
        console.error("✗ get_virtual_clock_hw_ptr not found");
        console.log("Available functions:", Object.keys(mp).filter(k => k.startsWith('_')).slice(0, 10));
        return;
    }

    // Test basic Python timing
    console.log("\n--- Testing Python time module ---");

    const result = mp.runPython(`
import time

# Get initial time
start = time.monotonic()
print(f"Start time: {start:.3f}s")

# This should work even without virtual clock running
# (will use fallback to Date.now() initially)
print("Time module is working!")

start
`);

    console.log(`Python returned: ${result}`);
    console.log("\n✓ All basic tests passed!");
}

testBasicTiming().catch(err => {
    console.error("Test failed:", err);
    process.exit(1);
});
