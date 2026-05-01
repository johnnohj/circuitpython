# CircuitPython-compatible WASM Port, mkIII

**The following discusses an independent project that is not an official
CircuitPython port. Though it aspires to provide true port quality, it is
currently an experimental work-in-progress and not officially supported or
maintained.**

CircuitPython running in the browser and Node.js via WebAssembly.

## Quick Start

```bash
# Build standard variant (CLI, no display)
make VARIANT=standard

# Build browser variant (display + board UI)
make VARIANT=browser

# Run tests
make test
```

Requires [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) (v32+).
See source repo [README](https://github.com/WebAssembly/wasi-sdk/blob/main/README.md#install) for fuller installation and configuration details

## Build steps

From the repository root:

    $ make -C mpy-cross
    $ cd ports/wasm
    $ make submodules
    $ make                     # standard variant (WASI CLI)
    $ make VARIANT=browser     # browser variant

Outputs:
- `build-standard/circuitpython.wasm` — WASI binary for Node.js
- `build-browser/circuitpython.wasm` — browser board binary

## Architecture

```
JS runtime (host)
  └─ wasm layer (ALL port-specific C code)
      └─ CircuitPython (supervisor/shared/, shared-bindings/, shared-module/)
          └─ MicroPython VM (py/)
              └─ Python (code.py)
```

The JS runtime acts as a **coprocessor** — it handles DOM events,
canvas rendering, serial I/O, and external hardware bridges while the
VM runs Python bytecode in WASM.  Communication uses shared linear
memory (MEMFS) with dirty flags and an event ring.

See `design/wasm-layer.md` and `design/js-coprocessor.md` for details.

## Key files

| File | Purpose |
|------|---------|
| `mpconfigboard.h` | CIRCUITPY_* feature flags |
| `port/main.c` | port_init / port_frame / hal_init / hal_frame |
| `port/port_memory.h` | port_memory_t — single struct owns all port state |
| `js/circuitpython.mjs` | JS API: CircuitPython.create(), lifecycle, contexts |
| `js/wasi.js` | in-memory WASI runtime + IndexedDB persistence |
| `boards/wasm_browser/definition.json` | default board pin layout |

## Variants

Both variants share the same hardware modules (digitalio, analogio,
busio, etc.).  The only difference is the display pipeline.

| Variant | Size | Display | Use case |
|---------|------|---------|----------|
| standard | ~1.1M | No | Node.js CLI testing |
| browser | ~1.5M | framebufferio + terminalio + Blinka | Browser with board UI |

## Hardware simulation

Virtual hardware state lives in WASM linear memory as flat arrays
(GPIO direction/value, analog values, PWM duty cycles, NeoPixel
buffers). JS reads/writes these via MEMFS.

## Filesystem

The port uses WASI POSIX filesystem calls (`fd_read`, `fd_write`,
etc.) backed by an in-memory filesystem (`wasi.js`). The
`/CIRCUITPY/` drive is optionally persisted to IndexedDB across
browser page reloads. No block device or FAT filesystem is used.
