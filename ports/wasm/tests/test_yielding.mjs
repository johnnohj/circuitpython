// Test CircuitPython's native yielding mechanism
// This demonstrates that time.sleep() yields control to JavaScript

import {loadCircuitPython} from '../build-standard/circuitpython.mjs';

console.log('=== CircuitPython Yielding Test ===\n');

// Track JavaScript events during Python sleep
let jsEventCount = 0;
let jsEventsProcessed = [];

// Create the WASM module with a mock message queue handler
const mp = await loadCircuitPython({
    stdin: () => '',

    // Mock handler for WASM requests
    onWASMRequest: (requestId, type, params) => {
        console.log(`[JS] Received request ${requestId}, type ${type}`);

        // For now, immediately complete all requests
        // In a real implementation, these would be async
        if (mp._wasm_complete_request) {
            const responseData = new Uint8Array(256);
            mp._wasm_complete_request(requestId, responseData.buffer, responseData.length);
        }
    }
});

// Set up a timer that fires during Python sleep
const intervalId = setInterval(() => {
    jsEventCount++;
    jsEventsProcessed.push(Date.now());
    console.log(`[JS] Event #${jsEventCount} processed`);
}, 100);  // Fire every 100ms

console.log('Starting Python code that sleeps for 1 second...\n');

// Run Python code with time.sleep()
try {
    mp.runPython(`
import time

print("Python: About to sleep for 1 second")
start = time.monotonic()

time.sleep(1.0)

end = time.monotonic()
print(f"Python: Woke up after {end - start:.3f} seconds")
print("Python: Done")
`);
} catch (e) {
    console.error('Error running Python:', e);
}

// Clean up
clearInterval(intervalId);

console.log(`\n=== Results ===`);
console.log(`JavaScript events processed during Python sleep: ${jsEventCount}`);
console.log(`Expected: ~10 events (1 second / 100ms interval)`);

if (jsEventCount >= 8) {
    console.log('✅ Yielding works! JavaScript executed during Python sleep.');
} else {
    console.log('❌ Yielding may not be working. Too few JavaScript events.');
}

console.log('\n=== Test Complete ===');
