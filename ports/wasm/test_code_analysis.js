// Test the analyze_code_structure function from WASM

const testCode = `import board
import digitalio
import time

led = digitalio.DigitalInOut(board.LED)
led.direction = digitalio.Direction.OUTPUT

while True:
    led.value = not led.value
    print('LED toggled!')
    time.sleep(1)
`;

async function test() {
    console.log('Loading CircuitPython WASM...');

    const { loadCircuitPython } = await import('./build-standard/circuitpython.mjs');
    const Module = await loadCircuitPython({
        heapsize: 1024 * 1024
    });

    console.log('\nTesting analyze_code_structure...');

    // Check if function exists
    if (typeof Module._analyze_code_structure === 'function') {
        console.log('✓ analyze_code_structure function found');

        // Allocate memory for the code string
        const codePtr = Module.allocateUTF8(testCode);
        const codeLen = testCode.length;

        // Call the function
        console.log('\nCalling analyze_code_structure...');
        const resultPtr = Module._analyze_code_structure(codePtr, codeLen);

        if (resultPtr) {
            // Read the result struct
            // typedef struct {
            //     bool has_while_true_loop;
            //     size_t while_true_line;
            //     size_t while_true_column;
            //     bool has_async_def;
            //     bool has_await;
            //     bool has_asyncio_run;
            //     int token_count;
            // } code_structure_t;

            const has_while_true = Module.HEAPU8[resultPtr];
            const while_true_line = Module.HEAPU32[(resultPtr + 4) / 4];
            const while_true_column = Module.HEAPU32[(resultPtr + 8) / 4];
            const has_async_def = Module.HEAPU8[resultPtr + 12];
            const has_await = Module.HEAPU8[resultPtr + 13];
            const has_asyncio_run = Module.HEAPU8[resultPtr + 14];
            const token_count = Module.HEAP32[(resultPtr + 16) / 4];

            console.log('\n✓ Result:');
            console.log(`  has_while_true_loop: ${!!has_while_true}`);
            console.log(`  while_true_line: ${while_true_line}`);
            console.log(`  while_true_column: ${while_true_column}`);
            console.log(`  has_async_def: ${!!has_async_def}`);
            console.log(`  has_await: ${!!has_await}`);
            console.log(`  has_asyncio_run: ${!!has_asyncio_run}`);
            console.log(`  token_count: ${token_count}`);

            if (has_while_true && while_true_line === 8) {
                console.log('\n✅ SUCCESS! Detected while True: loop at line 8');
            } else {
                console.log('\n❌ FAILED! Expected while True at line 8');
            }
        } else {
            console.log('❌ analyze_code_structure returned NULL');
        }

        // Free the allocated memory
        Module._free(codePtr);

    } else {
        console.log('❌ analyze_code_structure function NOT found');
        console.log('Available functions:', Object.keys(Module).filter(k => k.startsWith('_analyze')));
    }
}

test().catch(err => {
    console.error('Error:', err);
    process.exit(1);
});
