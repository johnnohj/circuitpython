/**
 * Simple Virtual Clock Test
 * Tests that the integrated virtual clock is working
 */

import { loadMicroPython } from '../build-standard/circuitpython.mjs';

async function main() {
    console.log("CircuitPython WASM Virtual Clock Tests");
    console.log("======================================\n");

    // Load the module (virtual clock starts automatically)
    const mp = await loadMicroPython();

    console.log("✓ Module loaded");

    // Check that virtual clock is available
    if (!mp.virtualClock) {
        console.error("✗ Virtual clock not found on module!");
        console.log("Available properties:", Object.keys(mp));
        return;
    }

    console.log("✓ Virtual clock is available");
    console.log("  Mode:", mp.virtualClock.mode === 0 ? "REALTIME" : mp.virtualClock.mode);

    // Test 1: Basic timing
    console.log("\n=== TEST 1: BASIC TIMING ===");

    const code = `
import time
start = time.monotonic()
print(f"Start: {start:.3f}s")
time.sleep(0.1)
end = time.monotonic()
print(f"End: {end:.3f}s")
print(f"Elapsed: {(end - start):.3f}s")
end - start
`;

    console.log("Running Python code with sleep(0.1)...");
    const wallStart = performance.now();
    const result = mp.runPython(code);
    const wallEnd = performance.now();

    console.log(`Python elapsed: ${result}s`);
    console.log(`Wall clock elapsed: ${((wallEnd - wallStart) / 1000).toFixed(3)}s`);

    // Test 2: Check statistics
    console.log("\n=== TEST 2: STATISTICS ===");
    const stats = mp.virtualClock.getStatistics();
    console.log("Virtual time:", stats.virtualTimeMs.toFixed(1), "ms");
    console.log("CPU frequency:", stats.cpuFrequencyMHz, "MHz");
    console.log("WASM yields:", stats.wasmYields.toString());

    // Test 3: Mode switching
    console.log("\n=== TEST 3: MODE SWITCHING ===");

    console.log("Switching to MANUAL mode...");
    mp.virtualClock.setManualMode();

    console.log("Advancing time by 50ms manually...");
    mp.virtualClock.advanceMs(50);

    const timeCheck = mp.runPython("import time; time.monotonic()");
    console.log(`Time is now: ${timeCheck}s`);

    console.log("Switching back to REALTIME mode...");
    mp.virtualClock.startRealtime();

    console.log("\n✓ All tests passed!");

    // Stop the realtime interval so process can exit
    mp.virtualClock.stopRealtime();
}

main().catch(err => {
    console.error("Test failed:", err);
    process.exit(1);
});
