/**
 * Test/Example for Virtual Clock System
 *
 * Demonstrates the three timing modes:
 * 1. Realtime - for interactive demos
 * 2. Manual - for step-by-step education
 * 3. Fast-forward - for testing
 */

import { loadCircuitPython } from '../build-standard/circuitpython.mjs';

// Note: VirtualClock is now automatically initialized by the module!
// Access it via mp.virtualClock

// Sample Python code that uses time
const pythonCode = `
import time
import board
import digitalio

print("Virtual Clock Demo!")
print("===================")

# Simple blink without delay
for i in range(5):
    print(f"Loop {i}: T={time.monotonic():.3f}s")
    time.sleep(0.5)

print(f"Final time: {time.monotonic():.3f}s")
`;

async function testRealtimeMode() {
    console.log("\n=== TEST 1: REALTIME MODE ===");
    console.log("Time should advance 1:1 with wall clock");

    const ctpy = await loadCircuitPython();
    const clock = mp.virtualClock;

    // Clock is already in realtime mode by default
    console.log("Virtual clock is running in REALTIME mode");

    // Run Python code
    const startWall = performance.now();
    ctpy.runPython(pythonCode);
    const endWall = performance.now();

    const virtualTime = clock.getCurrentTimeMs();
    const wallTime = endWall - startWall;

    console.log(`Virtual time: ${virtualTime.toFixed(1)}ms`);
    console.log(`Wall time: ${wallTime.toFixed(1)}ms`);
    console.log(`Ratio: ${(virtualTime / wallTime).toFixed(2)}x (should be ~1.0)`);
}

async function testManualMode() {
    console.log("\n=== TEST 2: MANUAL MODE ===");
    console.log("Step through code one millisecond at a time");

    const ctpy = await loadCircuitPython();
    const clock = mp.virtualClock;

    clock.setManualMode();

    // Set up Python code that prints time
    ctpy.runPython(`
import time
print("Starting at:", time.monotonic())
    `);

    // Manually advance time in steps
    for (let i = 0; i < 5; i++) {
        clock.advanceMs(100);  // Advance 100ms
        ctpy.runPython(`
import time
print(f"  Step {${i}}: T={time.monotonic():.3f}s")
        `);
    }

    // Show timeline
    console.log("\nTimeline:");
    clock.getFormattedTimeline().forEach(line => console.log("  " + line));
}

async function testFastForwardMode() {
    console.log("\n=== TEST 3: FAST-FORWARD MODE ===");
    console.log("Skip all time.sleep() delays");

    const ctpy = await loadCircuitPython();
    const clock = new VirtualClock(mp, mp.wasmMemory);

    clock.setFastForwardMode();

    const startWall = performance.now();

    // This should complete instantly even though it sleeps 10 seconds!
    ctpy.runPython(`
import time
print("Sleeping for 10 seconds...")
start = time.monotonic()
time.sleep(10)
end = time.monotonic()
print(f"Virtual time elapsed: {end - start:.1f}s")
    `);

    const endWall = performance.now();
    const wallTime = endWall - startWall;

    console.log(`Wall time: ${wallTime.toFixed(1)}ms (should be instant!)`);
    console.log(`Virtual time: ${clock.getCurrentTimeS().toFixed(1)}s (should be ~10s)`);
}

async function testEducatorWorkflow() {
    console.log("\n=== TEST 4: EDUCATOR WORKFLOW ===");
    console.log("Show student exactly what happens at each time step");

    const ctpy = await loadCircuitPython();
    const clock = new VirtualClock(mp, mp.wasmMemory);

    clock.setManualMode();

    // Student's code: blink an LED
    ctpy.runPython(`
import digitalio
import board
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT
    `);

    // Step through each action
    const steps = [
        { ms: 0, action: "led.value = True", description: "LED turns ON" },
        { ms: 500, action: "led.value = False", description: "LED turns OFF" },
        { ms: 1000, action: "led.value = True", description: "LED turns ON again" },
    ];

    for (const step of steps) {
        clock.advanceToMs(step.ms);
        clock.recordEvent(step.description);

        console.log(`\n[T=${step.ms}ms] ${step.description}`);
        console.log(`  Executing: ${step.action}`);
        mp.runPython(step.action);

        // Show stats
        const stats = clock.getStatistics();
        console.log(`  Virtual time: ${stats.virtualTimeMs.toFixed(1)}ms`);
        console.log(`  WASM yields: ${stats.wasmYields}`);
    }

    // Show complete timeline
    console.log("\n=== Complete Timeline ===");
    clock.getFormattedTimeline().forEach(line => console.log(line));
}

// Run all tests
async function main() {
    console.log("CircuitPython WASM Virtual Clock Tests");
    console.log("======================================\n");

    try {
        await testRealtimeMode();
        await testManualMode();
        await testFastForwardMode();
        await testEducatorWorkflow();

        console.log("\n✓ All tests completed!");
    } catch (error) {
        console.error("Test failed:", error);
    }
}

main();
