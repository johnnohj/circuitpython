/**
 * test_display.mjs — Run the worker, capture the framebuffer, serve it.
 *
 * 1. Starts the worker variant in a worker_thread
 * 2. Waits for it to render the terminal (Blinka + REPL prompt)
 * 3. Reads /hw/display/fb (RGB565)
 * 4. Serves an HTML page that renders it on a Canvas
 *
 * Usage: node test_display.mjs
 * Then open http://localhost:8080
 */

import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import { readFile, writeFile, mkdir, readdir, copyFile } from 'node:fs/promises';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { createServer } from 'node:http';
import { WASI } from 'node:wasi';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const WORKER_WASM = join(__dirname, 'build-worker/circuitpython.wasm');
const DISPLAY_W = 320;
const DISPLAY_H = 240;

// ── Worker thread ───────────────────────────────────────────────────
if (!isMainThread) {
    const { rootDir } = workerData;

    const wasi = new WASI({
        version: 'preview1',
        args: ['circuitpython-worker'],
        preopens: { '/': rootDir },
        returnOnExit: true,
    });

    try {
        const wasmBytes = readFileSync(WORKER_WASM);
        const compiled = new WebAssembly.Module(wasmBytes);
        const instance = new WebAssembly.Instance(compiled, wasi.getImportObject());
        const exitCode = wasi.start(instance);
        parentPort.postMessage({ type: 'exit', code: exitCode });
    } catch (e) {
        parentPort.postMessage({ type: 'error', message: e.message });
    }
    process.exit(0);
}

// ── Main thread ─────────────────────────────────────────────────────

async function setupRoot() {
    const rootDir = join(tmpdir(), 'cpywasi-display-' + Date.now());
    mkdirSync(rootDir, { recursive: true });

    for (const d of [
        'lib', 'modules',
        'hw', 'hw/gpio', 'hw/analog', 'hw/pwm', 'hw/neopixel',
        'hw/i2c', 'hw/i2c/dev', 'hw/spi',
        'hw/uart', 'hw/uart/0',
        'hw/display', 'hw/repl', 'hw/events',
    ]) {
        mkdirSync(join(rootDir, d), { recursive: true });
    }

    // Initial control file
    writeFileSync(join(rootDir, 'hw/control'), Buffer.alloc(32));

    return rootDir;
}

async function main() {
    const rootDir = await setupRoot();
    console.log('Root:', rootDir);

    // Start worker
    console.log('Starting worker...');
    const worker = new Worker(fileURLToPath(import.meta.url), {
        workerData: { rootDir },
    });

    worker.on('message', msg => console.log('Worker:', JSON.stringify(msg)));
    worker.on('error', err => console.error('Worker error:', err.message));

    // Wait for worker to render a frame
    console.log('Waiting for framebuffer...');
    const fbPath = join(rootDir, 'hw/display/fb');
    let attempts = 0;
    while (attempts < 50) {
        await new Promise(r => setTimeout(r, 100));
        if (existsSync(fbPath)) {
            const fb = readFileSync(fbPath);
            if (fb.length > 0) {
                console.log(`Framebuffer captured: ${fb.length} bytes`);
                break;
            }
        }
        attempts++;
    }

    if (attempts >= 50) {
        console.log('No framebuffer after 5s — worker may not have rendered.');
        console.log('Continuing anyway to serve the page...');
    }

    // Send SIG_TERM
    const ctrlPath = join(rootDir, 'hw/control');
    const ctrl = readFileSync(ctrlPath);
    ctrl[0] = 0x05;
    writeFileSync(ctrlPath, ctrl);

    // Wait for worker to exit
    await new Promise(r => setTimeout(r, 1000));

    // Read the final framebuffer
    let fbData = null;
    if (existsSync(fbPath)) {
        fbData = readFileSync(fbPath);
        console.log(`Final framebuffer: ${fbData.length} bytes (expected ${DISPLAY_W * DISPLAY_H * 2})`);
    }

    // Serve HTML page
    const html = `<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>CircuitPython WASI — Terminal Display</title>
<style>
body { background: #1a1a2e; display: flex; flex-direction: column;
       align-items: center; justify-content: center; height: 100vh;
       margin: 0; font-family: monospace; color: #e0e0e0; }
canvas { image-rendering: pixelated; border: 2px solid #0f3460;
         background: #000; }
h1 { font-size: 16px; color: #e94560; margin-bottom: 12px; }
p { font-size: 12px; color: #888; margin-top: 8px; }
</style>
</head>
<body>
<h1>CircuitPython WASI — Terminal Display (${DISPLAY_W}x${DISPLAY_H})</h1>
<canvas id="c" width="${DISPLAY_W}" height="${DISPLAY_H}"
        style="width:${DISPLAY_W*2}px;height:${DISPLAY_H*2}px"></canvas>
<p>Framebuffer: ${fbData ? fbData.length : 0} bytes (RGB565)</p>
<script>
fetch('/fb').then(r => r.arrayBuffer()).then(buf => {
    const fb = new Uint8Array(buf);
    const canvas = document.getElementById('c');
    const ctx = canvas.getContext('2d');
    const img = ctx.createImageData(${DISPLAY_W}, ${DISPLAY_H});
    const px = img.data;
    const w = ${DISPLAY_W}, h = ${DISPLAY_H};
    for (let i = 0; i < w * h; i++) {
        const lo = fb[i * 2], hi = fb[i * 2 + 1];
        const rgb565 = (hi << 8) | lo;
        px[i*4]   = ((rgb565 >> 11) & 0x1F) << 3;
        px[i*4+1] = ((rgb565 >> 5) & 0x3F) << 2;
        px[i*4+2] = (rgb565 & 0x1F) << 3;
        px[i*4+3] = 255;
    }
    ctx.putImageData(img, 0, 0);
});
</script>
</body>
</html>`;

    const server = createServer((req, res) => {
        if (req.url === '/fb' && fbData) {
            res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
            res.end(fbData);
        } else {
            res.writeHead(200, { 'Content-Type': 'text/html' });
            res.end(html);
        }
    });

    server.listen(8080, () => {
        console.log('\nOpen http://localhost:8080 to see the terminal display');
        console.log('Press Ctrl-C to exit');
    });
}

main().catch(e => { console.error(e); process.exit(1); });
