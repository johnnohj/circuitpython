/**
 * test_two_instance.mjs — Test reactor + hardware worker via shared filesystem.
 *
 * Main thread:  runs standard variant with a test script (the "reactor")
 * Worker thread: runs worker variant (hardware controller)
 * Both share the same temp directory as their WASI filesystem root,
 * which simulates the OPFS DMA bus.
 *
 * Usage: node test_two_instance.mjs
 */

import { readFile, writeFile, mkdir, readdir, copyFile } from 'node:fs/promises';
import { readFileSync, existsSync } from 'node:fs';
import { WASI } from 'node:wasi';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));

// ── Worker thread: runs the hardware worker WASM ────────────────────
if (!isMainThread) {
    const { rootDir, wasmPath } = workerData;

    const wasi = new WASI({
        version: 'preview1',
        args: ['circuitpython-worker'],
        preopens: { '/': rootDir },
        returnOnExit: true,
    });

    try {
        const wasmBytes = readFileSync(wasmPath);
        const compiled = new WebAssembly.Module(wasmBytes);
        const instance = new WebAssembly.Instance(compiled, wasi.getImportObject());
        const exitCode = wasi.start(instance);
        parentPort.postMessage({ type: 'exit', code: exitCode });
    } catch (e) {
        parentPort.postMessage({ type: 'error', message: e.message });
    }
    process.exit(0);
}

// ── Main thread: orchestrate test ───────────────────────────────────

const STANDARD_WASM = join(__dirname, 'build-standard/circuitpython.wasm');
const WORKER_WASM = join(__dirname, 'build-worker/circuitpython.wasm');

async function setupRoot() {
    const rootDir = join(tmpdir(), 'cpywasi-two-' + Date.now());
    await mkdir(rootDir, { recursive: true });

    // Create directory tree
    for (const d of [
        'lib', 'modules',
        'hw', 'hw/gpio', 'hw/analog', 'hw/pwm', 'hw/neopixel',
        'hw/i2c', 'hw/i2c/dev', 'hw/spi',
        'hw/uart', 'hw/uart/0',
        'hw/display', 'hw/repl', 'hw/events',
        'hw/board',
    ]) {
        await mkdir(join(rootDir, d), { recursive: true });
    }

    // Copy Python shim modules
    const modulesDir = join(__dirname, 'modules');
    if (existsSync(modulesDir)) {
        for (const f of await readdir(modulesDir)) {
            if (f.endsWith('.py')) {
                await copyFile(join(modulesDir, f), join(rootDir, 'modules', f));
            }
        }
    }

    // Write initial control file (32 bytes of zeros)
    await writeFile(join(rootDir, 'hw/control'), Buffer.alloc(32));

    return rootDir;
}

async function runReactor(rootDir, scriptContent) {
    // Write the test script
    await writeFile(join(rootDir, 'test_script.py'), scriptContent);

    const wasi = new WASI({
        version: 'preview1',
        args: ['circuitpython', '/test_script.py'],
        preopens: { '/': rootDir },
        returnOnExit: true,
    });

    const wasmBytes = await readFile(STANDARD_WASM);
    const compiled = await WebAssembly.compile(wasmBytes);
    const instance = await WebAssembly.instantiate(compiled, wasi.getImportObject());

    try {
        return wasi.start(instance);
    } catch (e) {
        if (e.message?.includes('exit')) return 0;
        throw e;
    }
}

function sendSignal(rootDir, sig) {
    const controlPath = join(rootDir, 'hw/control');
    const buf = Buffer.alloc(32);
    try {
        const existing = readFileSync(controlPath);
        existing.copy(buf);
    } catch {}
    buf[0] = sig;
    require('fs').writeFileSync(controlPath, buf);
}

async function main() {
    console.log('=== Two-instance test: reactor + hardware worker ===\n');

    const rootDir = await setupRoot();
    console.log('Shared root:', rootDir);

    // ── Phase 1: Run reactor (standard variant) with test script ────
    // The reactor uses Python shims to write state to /hw/ endpoints.
    console.log('\n--- Phase 1: Reactor writes hardware state ---');

    const testScript = `
import sys
sys.path.insert(0, '/modules')

import board, digitalio, analogio, pwmio

# Write GPIO state
led = digitalio.DigitalInOut(board.LED)
led.switch_to_output(value=True)
print("Reactor: LED pin 13 set to True")

# Write analog state
aout = analogio.AnalogOut(board.A1)
aout.value = 42000
print("Reactor: AnalogOut pin 1 set to 42000")

# Write PWM state
pwm = pwmio.PWMOut(board.D5, duty_cycle=32768, frequency=1000)
print("Reactor: PWM pin 5 duty=32768 freq=1000")

# Write I2C device register
import busio
i2c = busio.I2C(board.SCL, board.SDA)
# Simulate a BMP280: write chip ID 0x58 at register 0xD0
import os
f = open('/hw/i2c/dev/118', 'wb')
f.write(b'\\xff' * 256)
f.close()
f = open('/hw/i2c/dev/118', 'r+b')
f.seek(0xD0)
f.write(b'\\x58')
f.close()
print("Reactor: BMP280 chip ID written at /hw/i2c/dev/118")

# Write UART data
uart = busio.UART(board.TX, board.RX)
uart.write(b'Hello from reactor\\n')
print("Reactor: UART TX written")

# Leave pins active (don't deinit) so worker can read state
print("Reactor: done writing state")
`;

    const exitCode = await runReactor(rootDir, testScript);
    console.log('Reactor exit code:', exitCode);

    // ── Phase 2: Verify OPFS files were written ─────────────────────
    console.log('\n--- Phase 2: Verify OPFS endpoint files ---');

    // Read GPIO state
    const gpioPath = join(rootDir, 'hw/gpio/state');
    if (existsSync(gpioPath)) {
        const gpio = readFileSync(gpioPath);
        // Pin 13 (LED): 6 bytes per pin, offset 13*6 = 78
        const pin13 = gpio.slice(78, 84);
        const [value, direction, pull, openDrain, enabled, neverReset] = pin13;
        console.log(`  GPIO pin 13: value=${value} direction=${direction} enabled=${enabled}`);
        console.assert(value === 1, 'LED should be ON (1)');
        console.assert(direction === 1, 'LED should be OUTPUT (1)');
        console.assert(enabled === 1, 'LED should be enabled');
    } else {
        console.log('  GPIO state file NOT FOUND');
    }

    // Read analog state
    const analogPath = join(rootDir, 'hw/analog/state');
    if (existsSync(analogPath)) {
        const analog = readFileSync(analogPath);
        // Pin 1: 4 bytes per pin, offset 1*4 = 4
        const val = analog.readUInt16LE(4);
        const isOutput = analog[6];
        const enabled = analog[7];
        console.log(`  Analog pin 1: value=${val} is_output=${isOutput} enabled=${enabled}`);
        console.assert(val === 42000, 'AnalogOut should be 42000');
        console.assert(isOutput === 1, 'Should be output');
    } else {
        console.log('  Analog state file NOT FOUND');
    }

    // Read PWM state
    const pwmPath = join(rootDir, 'hw/pwm/state');
    if (existsSync(pwmPath)) {
        const pwm = readFileSync(pwmPath);
        // Pin 5: 8 bytes per pin, offset 5*8 = 40
        const duty = pwm.readUInt16LE(40);
        const freq = pwm.readUInt32LE(42);
        const flags = pwm[46];
        console.log(`  PWM pin 5: duty=${duty} freq=${freq} flags=0x${flags.toString(16)}`);
        console.assert(duty === 32768, 'Duty should be 32768');
        console.assert(freq === 1000, 'Frequency should be 1000');
    } else {
        console.log('  PWM state file NOT FOUND');
    }

    // Read I2C device file
    const i2cPath = join(rootDir, 'hw/i2c/dev/118');
    if (existsSync(i2cPath)) {
        const regs = readFileSync(i2cPath);
        const chipId = regs[0xD0];
        console.log(`  I2C dev 118: reg 0xD0 = 0x${chipId.toString(16)}`);
        console.assert(chipId === 0x58, 'BMP280 chip ID should be 0x58');
    } else {
        console.log('  I2C device file NOT FOUND');
    }

    // Read UART TX
    const uartTxPath = join(rootDir, 'hw/uart/0/tx');
    if (existsSync(uartTxPath)) {
        const tx = readFileSync(uartTxPath, 'utf-8');
        console.log(`  UART TX: "${tx.trim()}"`);
        console.assert(tx.includes('Hello from reactor'), 'UART TX should contain message');
    } else {
        console.log('  UART TX file NOT FOUND');
    }

    console.log('\n--- Phase 3: Start hardware worker ---');

    // Start the worker in a worker_thread
    const worker = new Worker(fileURLToPath(import.meta.url), {
        workerData: { rootDir, wasmPath: WORKER_WASM },
    });

    // Give it a moment to initialize and read OPFS
    await new Promise(r => setTimeout(r, 2000));

    // Read control file to check worker state
    const controlPath = join(rootDir, 'hw/control');
    if (existsSync(controlPath)) {
        const ctrl = readFileSync(controlPath);
        const workerState = ctrl[1];
        const frameCount = ctrl.readUInt32LE(8);
        console.log(`  Worker state: ${workerState} (1=running), frames: ${frameCount}`);
    }

    // Send SIG_TERM
    console.log('  Sending SIG_TERM...');
    const termBuf = Buffer.alloc(1);
    termBuf[0] = 0x05;  // SIG_TERM
    const ctrlBuf = readFileSync(controlPath);
    ctrlBuf[0] = 0x05;
    const { writeFileSync } = await import('node:fs');
    writeFileSync(controlPath, ctrlBuf);

    // Wait for worker to exit
    const workerResult = await new Promise((resolve) => {
        const timeout = setTimeout(() => {
            worker.terminate();
            resolve({ type: 'timeout' });
        }, 5000);

        worker.on('message', (msg) => {
            clearTimeout(timeout);
            resolve(msg);
        });

        worker.on('exit', (code) => {
            clearTimeout(timeout);
            resolve({ type: 'exit', code });
        });

        worker.on('error', (err) => {
            clearTimeout(timeout);
            resolve({ type: 'error', message: err.message });
        });
    });

    console.log('  Worker result:', JSON.stringify(workerResult));

    console.log('\n=== Test complete ===');
    console.log('Shared root (inspect files):', rootDir);
}

main().catch(e => {
    console.error(e);
    process.exit(1);
});
