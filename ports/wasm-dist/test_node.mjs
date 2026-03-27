// Quick test: load circuitpython.wasm under Node.js WASI
import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';

const wasi = new WASI({
    version: 'preview1',
    args: ['circuitpython', '-c', 'print("hello from wasm-dist!")'],
    preopens: { '/': '.' },
});

const wasm = await WebAssembly.compile(await readFile('build-standard/circuitpython.wasm'));
const instance = await WebAssembly.instantiate(wasm, wasi.getImportObject());
try {
    wasi.start(instance);
} catch (e) {
    console.error('WASI error:', e.message);
}
