// Test the CircuitPython REPL banner
import {loadCircuitPython} from '../../build-standard/circuitpython.mjs';

const mp = await loadCircuitPython({stdin: () => ''});

// Initialize REPL
mp.replInit();

// Simulate CTRL-B to show banner
console.log('Simulating CTRL-B to show REPL banner:');
mp.replProcessChar(2);  // CTRL-B

console.log('\nDone!');
