/**
 * test_repl.mjs — Live interactive REPL with display on Canvas.
 *
 * Worker thread: runs CircuitPython worker variant (event-driven REPL)
 * Main thread:   HTTP server — serves HTML, framebuffer, keyboard input
 * Browser:       polls /fb at 10fps, sends keystrokes via POST /key
 *
 * Usage: node test_repl.mjs
 * Then open http://localhost:8081
 */

import { readFileSync, writeFileSync, mkdirSync, existsSync, appendFileSync } from 'node:fs';
import { readdir } from 'node:fs/promises';
import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { createServer } from 'node:http';
import { WASI } from 'node:wasi';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const __dirname = fileURLToPath(new URL('.', import.meta.url));
const WORKER_WASM = join(__dirname, 'build-worker/circuitpython.wasm');
const W = 480, H = 360;
const FB_SIZE = W * H * 2;

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
        const bytes = readFileSync(WORKER_WASM);
        const mod = new WebAssembly.Module(bytes);
        const inst = new WebAssembly.Instance(mod, wasi.getImportObject());
        const code = wasi.start(inst);
        parentPort.postMessage({ type: 'exit', code });
    } catch (e) {
        parentPort.postMessage({ type: 'error', message: e.message });
    }
    process.exit(0);
}

// ── Main thread ─────────────────────────────────────────────────────

function setupRoot() {
    const rootDir = join(tmpdir(), 'cpywasi-repl-' + Date.now());
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
    writeFileSync(join(rootDir, 'hw/control'), Buffer.alloc(32));
    writeFileSync(join(rootDir, 'hw/repl/rx'), Buffer.alloc(0));
    return rootDir;
}

async function main() {
    const rootDir = setupRoot();

    // Copy modules
    const modulesDir = join(__dirname, 'modules');
    if (existsSync(modulesDir)) {
        for (const f of await readdir(modulesDir)) {
            if (f.endsWith('.py')) {
                writeFileSync(
                    join(rootDir, 'modules', f),
                    readFileSync(join(modulesDir, f))
                );
            }
        }
    }

    console.log('Root:', rootDir);

    const rxPath = join(rootDir, 'hw/repl/rx');
    const fbPath = join(rootDir, 'hw/display/fb');

    // Start worker
    console.log('Starting worker...');
    const worker = new Worker(fileURLToPath(import.meta.url), {
        workerData: { rootDir },
    });
    worker.on('message', msg => console.log('Worker:', JSON.stringify(msg)));
    worker.on('error', err => console.error('Worker error:', err.message));
    worker.on('exit', code => console.log('Worker thread exited:', code));

    const html = `<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>CircuitPython WASI REPL</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { background: #1a1a2e; display: flex; flex-direction: column;
       align-items: center; justify-content: center; min-height: 100vh;
       font-family: monospace; color: #e0e0e0; }
canvas { image-rendering: pixelated; border: 2px solid #0f3460;
         cursor: text; outline: none; }
h1 { font-size: 16px; color: #e94560; margin-bottom: 8px; }
p { font-size: 12px; color: #888; margin-top: 8px; }
#status { font-size: 13px; color: #4ecca3; margin-top: 4px; }
</style>
</head>
<body>
<h1>CircuitPython WASI &mdash; Live REPL</h1>
<canvas id="c" width="${W}" height="${H}"
        style="width:${W * 1.5}px;height:${H * 1.5}px" tabindex="0"></canvas>
<p id="status">Loading...</p>
<p>Click canvas, then type Python. Ctrl-C, arrows, backspace work.</p>
<script>
const canvas = document.getElementById('c');
const ctx = canvas.getContext('2d');
const img = ctx.createImageData(${W}, ${H});
const status = document.getElementById('status');
canvas.focus();

let frameNum = 0;

function paintRGB565(fb) {
    const px = img.data;
    for (let i = 0; i < ${W * H}; i++) {
        const lo = fb[i*2], hi = fb[i*2+1];
        const v = (hi << 8) | lo;
        px[i*4]   = ((v >> 11) & 0x1F) << 3;
        px[i*4+1] = ((v >> 5) & 0x3F) << 2;
        px[i*4+2] = (v & 0x1F) << 3;
        px[i*4+3] = 255;
    }
    ctx.putImageData(img, 0, 0);

    // Show which rows have content
    const rowInfo = [];
    for (let y = 0; y < ${H}; y++) {
        let count = 0;
        for (let x = 0; x < ${W}; x++) {
            const off = (y * ${W} + x) * 2;
            if (fb[off] !== 0 || fb[off+1] !== 0) count++;
        }
        if (count > 3) rowInfo.push('y' + y + ':' + count);
    }
    status.textContent = 'Frame ' + (++frameNum) + ' | rows: ' + rowInfo.join(' ');
}

// Poll /fb at 10fps
async function poll() {
    try {
        const res = await fetch('/fb?t=' + Date.now());
        if (res.ok) {
            const buf = await res.arrayBuffer();
            if (buf.byteLength === ${FB_SIZE}) {
                paintRGB565(new Uint8Array(buf));
            }
        }
    } catch {}
}
setInterval(poll, 100);
setTimeout(poll, 500);  // first poll after worker has time to init

// Keyboard
canvas.addEventListener('keydown', (e) => {
    let bytes = null;
    if (e.ctrlKey && e.key === 'c') { bytes = [0x03]; e.preventDefault(); }
    else if (e.ctrlKey && e.key === 'd') { bytes = [0x04]; e.preventDefault(); }
    else if (e.key === 'Enter') { bytes = [0x0D]; }
    else if (e.key === 'Backspace') { bytes = [0x08]; e.preventDefault(); }
    else if (e.key === 'Tab') { bytes = [0x09]; e.preventDefault(); }
    else if (e.key === 'Escape') { bytes = [0x1B]; }
    else if (e.key === 'ArrowUp') { bytes = [0x1B, 0x5B, 0x41]; e.preventDefault(); }
    else if (e.key === 'ArrowDown') { bytes = [0x1B, 0x5B, 0x42]; e.preventDefault(); }
    else if (e.key === 'ArrowRight') { bytes = [0x1B, 0x5B, 0x43]; e.preventDefault(); }
    else if (e.key === 'ArrowLeft') { bytes = [0x1B, 0x5B, 0x44]; e.preventDefault(); }
    else if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) {
        bytes = Array.from(new TextEncoder().encode(e.key));
    }
    if (bytes) {
        fetch('/key', { method: 'POST', body: new Uint8Array(bytes) });
    }
});
</script>
</body>
</html>`;

    // HTTP server — dead simple
    const server = createServer((req, res) => {
        if (req.url.startsWith('/fb')) {
            // Always read fresh from disk
            try {
                const fb = readFileSync(fbPath);
                res.writeHead(200, {
                    'Content-Type': 'application/octet-stream',
                    'Content-Length': fb.length,
                    'Cache-Control': 'no-store',
                });
                res.end(fb);
            } catch {
                res.writeHead(200, {
                    'Content-Type': 'application/octet-stream',
                    'Content-Length': 0,
                });
                res.end();
            }
        } else if (req.url === '/key' && req.method === 'POST') {
            const chunks = [];
            req.on('data', c => chunks.push(c));
            req.on('end', () => {
                appendFileSync(rxPath, Buffer.concat(chunks));
                res.writeHead(200);
                res.end();
            });
        } else {
            res.writeHead(200, { 'Content-Type': 'text/html' });
            res.end(html);
        }
    });

    server.listen(8081, () => {
        console.log('\n  Open http://localhost:8081\n');
    });
}

main().catch(e => { console.error(e); process.exit(1); });
