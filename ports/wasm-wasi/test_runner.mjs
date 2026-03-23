/**
 * test_runner.mjs — Run CircuitPython WASI binary via Node.js WASI
 *
 * Usage: node test_runner.mjs [script.py]
 * If no script, runs interactive REPL on stdin.
 *
 * Provides WASI preview1 with preopened / for filesystem access.
 */

import { readFile, writeFile, mkdtemp, mkdir, readdir, copyFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { WASI } from 'node:wasi';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { argv } from 'node:process';

const wasmPath = join(import.meta.dirname, 'build-standard/circuitpython.wasm');
const scriptArg = argv[2];

async function main() {
    // Create a temp directory for the WASI filesystem root
    const rootDir = await mkdtemp(join(tmpdir(), 'cpywasi-'));
    await mkdir(join(rootDir, 'lib'), { recursive: true });

    // Copy frozen modules into /modules/ so shims are importable
    const modulesDir = join(import.meta.dirname, 'modules');
    const modulesDest = join(rootDir, 'modules');
    await mkdir(modulesDest, { recursive: true });
    if (existsSync(modulesDir)) {
        for (const f of await readdir(modulesDir)) {
            if (f.endsWith('.py')) {
                await copyFile(join(modulesDir, f), join(modulesDest, f));
            }
        }
    }

    // Create /hw/ directory tree
    for (const d of ['hw', 'hw/gpio', 'hw/analog', 'hw/pwm', 'hw/neopixel',
                     'hw/i2c', 'hw/i2c/dev', 'hw/spi',
                     'hw/uart', 'hw/uart/0',
                     'hw/display', 'hw/repl', 'hw/events']) {
        await mkdir(join(rootDir, d), { recursive: true });
    }

    // If a script file was provided, copy it into the temp root
    let args = ['circuitpython'];
    if (scriptArg) {
        const scriptContent = await readFile(scriptArg, 'utf-8');
        const scriptDest = join(rootDir, 'test_script.py');
        await writeFile(scriptDest, scriptContent);
        args.push('/test_script.py');
    }

    const wasi = new WASI({
        version: 'preview1',
        args: args,
        preopens: { '/': rootDir },
        returnOnExit: true,
    });

    const wasmBytes = await readFile(wasmPath);
    const compiled = await WebAssembly.compile(wasmBytes);
    const importObject = wasi.getImportObject();
    const instance = await WebAssembly.instantiate(compiled, importObject);

    try {
        const exitCode = wasi.start(instance);
        process.exit(exitCode);
    } catch (e) {
        if (e.message && e.message.includes('exit')) {
            // Normal exit
            process.exit(0);
        }
        console.error('WASM error:', e.message);
        process.exit(1);
    }
}

main().catch(e => {
    console.error(e);
    process.exit(1);
});
