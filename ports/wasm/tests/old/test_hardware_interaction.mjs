#!/usr/bin/env node

/**
 * CircuitPython WASM - Hardware Interaction Demo (CLI)
 *
 * Demonstrates how JavaScript can interact with CircuitPython WASM:
 * - JavaScript observes LED outputs
 * - JavaScript simulates button inputs
 * - WASM runs autonomously with internal virtual hardware
 */

import loadCircuitPython from '../../build-standard/circuitpython.mjs';

console.log('🔌 CircuitPython WASM - Hardware Interaction Demo\n');
console.log('Architecture: WASM runs self-sufficiently, JS acts as external world\n');

// Python code that reads a button and controls an LED
const pythonCode = `
import board
import digitalio
import time

print("Button + LED Demo")
print("=" * 40)

# Setup LED (output)
led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT
print("LED initialized as OUTPUT on D13")

# Setup button (input with pull-up)
button = digitalio.DigitalInOut(board.D2)
button.direction = digitalio.Direction.INPUT
button.pull = digitalio.Pull.UP
print("Button initialized as INPUT with PULL_UP on D2")

print("\\nWaiting for button presses...")
print("(JavaScript will simulate button presses)\\n")

# Main loop - button controls LED
for i in range(10):
    # Button is active-low (False when pressed)
    if not button.value:
        led.value = True
        print(f"  {i}: Button PRESSED -> LED ON")
    else:
        led.value = False

    time.sleep(0.5)

print("\\nDemo complete!")
led.deinit()
button.deinit()
`;

async function main() {
    // Load CircuitPython
    console.log('Loading CircuitPython WASM...');
    const ctpy = await loadCircuitPython({
        stdout: (text) => process.stdout.write(text),
        stderr: (text) => process.stderr.write(text)
    });

    console.log('CircuitPython loaded!\n');

    // Write the Python code
    ctpy.FS.writeFile('/test.py', pythonCode);

    // Run the Python code in the background
    console.log('Starting Python code...\n');
    setTimeout(() => {
        ctpy.runFile('/test.py');
    }, 100);

    // Simulate button presses from JavaScript
    console.log('\n[JavaScript] Simulating button interactions:\n');

    const buttonPresses = [
        { time: 1500, action: 'press' },
        { time: 2500, action: 'release' },
        { time: 3500, action: 'press' },
        { time: 4000, action: 'release' },
        { time: 4500, action: 'press' },
        { time: 5500, action: 'release' }
    ];

    buttonPresses.forEach(({ time, action }) => {
        setTimeout(() => {
            const pin = 2;
            if (action === 'press') {
                console.log(`[JS] -> Pressing button D${pin} (set to LOW)`);
                ctpy._virtual_gpio_set_input_value(pin, false);
            } else {
                console.log(`[JS] -> Releasing button D${pin} (set to HIGH)`);
                ctpy._virtual_gpio_set_input_value(pin, true);
            }

            // Read LED state
            const ledState = ctpy._virtual_gpio_get_output_value(13);
            console.log(`[JS] <- LED D13 is now: ${ledState ? 'ON' : 'OFF'}\n`);
        }, time);
    });

    // Show final state after program completes
    setTimeout(() => {
        console.log('\n' + '='.repeat(50));
        console.log('Final Hardware State:');
        console.log('='.repeat(50));

        // Check various pins
        const pins = [13, 12, 2, 3];
        pins.forEach(pin => {
            const direction = ctpy._virtual_gpio_get_direction(pin);
            const directionName = direction === 1 ? 'OUTPUT' : 'INPUT';

            if (direction === 1) {
                // Output
                const value = ctpy._virtual_gpio_get_output_value(pin);
                console.log(`  D${pin}: ${directionName}, Value: ${value ? 'HIGH' : 'LOW'}`);
            } else {
                // Input
                const pull = ctpy._virtual_gpio_get_pull(pin);
                const pullName = pull === 1 ? 'PULL_UP' : pull === 2 ? 'PULL_DOWN' : 'NONE';
                console.log(`  D${pin}: ${directionName}, Pull: ${pullName}`);
            }
        });

        console.log('\n✅ Demo complete!');
        console.log('\nKey Points:');
        console.log('  • WASM runs CircuitPython code independently');
        console.log('  • Virtual hardware state is inside WASM/C');
        console.log('  • JavaScript can observe outputs (LED states)');
        console.log('  • JavaScript can inject inputs (button presses)');
        console.log('  • No yielding or waiting - all operations are instant!');
    }, 6500);
}

main().catch(console.error);
