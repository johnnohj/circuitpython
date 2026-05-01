/**
 * cp-worker.js — runs circuitpython.wasm in a Web Worker
 *
 * Receives init message with wasmUrl + file data.
 * Runs chassis_frame on a 16ms timer.
 * Posts outbound packets (serial_tx, gpio, neopixel) to main thread.
 * Receives inbound messages (serial_rx, gpio input, exec, stop).
 */

// We can't use ES module imports in a classic Worker, so we inline
// the minimal WASI + setup logic needed.

let vm = null;       // WASM exports
let wasi = null;     // WasiMemfs instance
let regions = {};    // path → { ptr, size }
let running = false;
let frameTimer = null;

// ── Message handler ──

self.onmessage = async (e) => {
    const msg = e.data;

    switch (msg.type) {
        case 'init':
            await init(msg);
            break;
        case 'exec_file':
            execFile(msg.path);
            break;
        case 'start_repl':
            vm.cp_start_repl();
            break;
        case 'serial_push':
            vm.cp_serial_push(msg.byte);
            break;
        case 'ctrl_c':
            vm.cp_ctrl_c();
            break;
        case 'ctrl_d':
            vm.cp_ctrl_d();
            break;
        case 'gpio_input':
            setGpioInput(msg.pin, msg.value);
            break;
        case 'write_file':
            wasi.writeFile(msg.path, new Uint8Array(msg.data));
            break;
        case 'stop':
            vm.cp_ctrl_c();
            vm.chassis_frame((performance.now() * 1000) | 0, 13000);
            vm.cp_cleanup?.();
            break;
    }
};

// ── Init ──

async function init(msg) {
    // Dynamically import WasiMemfs (Worker supports dynamic import)
    const { WasiMemfs } = await import('./wasi.js');

    wasi = new WasiMemfs({
        args: ['circuitpython'],
        onStdout: () => {},
        onStderr: (text) => self.postMessage({ type: 'stderr', text }),
    });

    const pendingRegistrations = [];

    const imports = {
        ...wasi.getImports(),
        ffi: {
            request_frame: () => {},
        },
        memfs: {
            register: (pathPtr, pathLen, dataPtr, dataSize) => {
                pendingRegistrations.push({ pathPtr, pathLen, dataPtr, dataSize });
            },
        },
        jsffi: makeJsffiStubs(),
        port: {
            registerPinListener: () => {},
            unregisterPinListener: () => {},
            getCpuTemperature: () => 25,
            getCpuVoltage: () => 3300,
        },
    };

    const wasmBytes = await fetch(msg.wasmUrl).then(r => r.arrayBuffer());
    const module = await WebAssembly.compile(wasmBytes);
    const instance = await WebAssembly.instantiate(module, imports);
    wasi.setInstance(instance);
    vm = instance.exports;

    // Write initial files
    if (msg.files) {
        for (const [path, content] of Object.entries(msg.files)) {
            wasi.writeFile(path, new TextEncoder().encode(content));
        }
    }

    // Initialize VM
    if (vm._initialize) vm._initialize();
    vm.chassis_init();

    // Flush registrations
    const mem = vm.memory;
    for (const { pathPtr, pathLen, dataPtr, dataSize } of pendingRegistrations) {
        const path = new TextDecoder().decode(
            new Uint8Array(mem.buffer, pathPtr, pathLen));
        regions[path] = { ptr: dataPtr, size: dataSize };
    }

    // Run init frame
    vm.chassis_frame(0, 13000);

    // Gather display info for main thread
    displayInfo = {
        fbAddr: vm.wasm_display_fb_addr(),
        fbWidth: vm.wasm_display_fb_width(),
        fbHeight: vm.wasm_display_fb_height(),
        frameCountAddr: vm.wasm_display_frame_count_addr(),
    };

    // Start frame loop
    running = true;
    frameTimer = setInterval(frame, 16);

    self.postMessage({ type: 'ready', regions, displayInfo });
}

// ── Frame loop ──

function frame() {
    if (!running || !vm) return;

    const nowUs = (performance.now() * 1000) | 0;
    vm.chassis_frame(nowUs, 13000);

    // Post outbound packet to main thread
    const packet = buildOutboundPacket();
    if (packet) {
        self.postMessage({ type: 'frame', packet }, packet.transfers);
    }
}

let lastFrameCount = 0;
let displayInfo = null;

function buildOutboundPacket() {
    const mem = vm.memory.buffer;
    const packet = { transfers: [] };

    // Serial TX — drain ring
    const txReg = regions['/hal/serial/tx'];
    if (txReg) {
        const view = new DataView(mem);
        const writeHead = view.getUint32(txReg.ptr, true);
        let readHead = view.getUint32(txReg.ptr + 4, true);
        if (readHead !== writeHead) {
            const bytes = new Uint8Array(mem);
            const dataBase = txReg.ptr + 8;
            const cap = txReg.size - 8;
            const chars = [];
            while (readHead !== writeHead) {
                chars.push(bytes[dataBase + readHead]);
                readHead = (readHead + 1) % cap;
            }
            view.setUint32(txReg.ptr + 4, readHead, true);
            packet.serial = new Uint8Array(chars);
        }
    }

    // GPIO — copy snapshot
    const gpioReg = regions['/hal/gpio'];
    if (gpioReg) {
        packet.gpio = new Uint8Array(mem.slice(gpioReg.ptr, gpioReg.ptr + gpioReg.size));
    }

    // NeoPixel — copy snapshot
    const neoReg = regions['/hal/neopixel'];
    if (neoReg) {
        packet.neopixel = new Uint8Array(mem.slice(neoReg.ptr, neoReg.ptr + neoReg.size));
    }

    // Framebuffer — only when display changed
    if (displayInfo) {
        const view = new DataView(mem);
        const fc = view.getUint32(displayInfo.frameCountAddr, true);
        if (fc !== lastFrameCount) {
            lastFrameCount = fc;
            const fbSize = displayInfo.fbWidth * displayInfo.fbHeight * 2; // RGB565
            const fb = new Uint8Array(mem.slice(displayInfo.fbAddr, displayInfo.fbAddr + fbSize));
            packet.framebuffer = fb;
            packet.fbWidth = displayInfo.fbWidth;
            packet.fbHeight = displayInfo.fbHeight;
            packet.transfers.push(fb.buffer);
        }
    }

    return packet;
}

// ── Inbound operations ──

function execFile(path) {
    const enc = new TextEncoder();
    const bytes = enc.encode(path);
    const addr = vm.cp_input_buf_addr();
    new Uint8Array(vm.memory.buffer).set(bytes, addr);
    vm.cp_exec(1, bytes.length);
}

function setGpioInput(pin, value) {
    const gpioReg = regions['/hal/gpio'];
    if (gpioReg) {
        new Uint8Array(vm.memory.buffer)[gpioReg.ptr + pin * 12 + 2] = value ? 1 : 0;
    }
}

function makeJsffiStubs() {
    return {
        check_existing: () => 0,
        calln_kw: () => 0,
        call1: () => 0,
        calln: () => 0,
        call0: () => 0,
        lookup_attr: () => 0,
        store_attr: () => {},
        subscr_load: () => 0,
        subscr_store: () => {},
        has_attr: () => 0,
        get_iter: () => 0,
        free_ref: () => {},
        reflect_construct: () => 0,
        iter_next: () => 0,
        create_pyproxy: () => 0,
        to_js: () => 0,
    };
}
