CircuitPython WASM Port
=======================

This port runs CircuitPython as a WebAssembly module in browsers and
Node.js. It provides a virtual CircuitPython board with simulated
hardware (GPIO, analog, PWM, NeoPixels, I2C, display) driven by
JavaScript, so users can run CircuitPython code without physical
hardware.

The guiding principle: **if it works on real hardware, it should work
here.** The port targets the same shared-bindings API as physical
boards; differences arise from timing, JavaScript runtime behavior,
and the virtual nature of the hardware.

For project background and design discussion, see `DEV_NOTES.md`.
For architecture documents, see the `design/` directory.

Visit the sample on [GitHub pages](https://johnnohj.github.io/wasm/) to try out a recent build!

Variants
--------

| Variant    | Purpose | Key modules |
|------------|---------|-------------|
| `standard` | WASI CLI — Node.js testing, REPL | time, random, os, math, json, re, collections |
| `browser`  | Full board simulation in browser | + displayio, digitalio, analogio, pwmio, neopixel_write, busio, board |

The `browser` variant adds a virtual board with 64 GPIO pins, a
160×128 framebuffer display, NeoPixel output, I2C/SPI/UART buses,
and a board module matching `boards/wasm_browser/definition.json`.

Building
--------

### Dependencies

- [wasi-sdk](https://github.com/aspect-build/aspect-build-wasi-sdk)
  (default path: `~/.local/bin/wasi-sdk-30.0-x86_64-linux`; override
  with `WASI_SDK=`)
- Python 3.x
- `mpy-cross` (built from the repo)

No system `libffi` or `pkg-config` is needed — this port compiles
entirely with wasi-sdk's clang.

### Build steps

From the repository root:

    $ make -C mpy-cross
    $ cd ports/wasm
    $ make submodules
    $ make                     # standard variant (WASI CLI)
    $ make VARIANT=browser     # browser variant

Outputs:
- `build-standard/circuitpython.wasm` — WASI binary for Node.js
- `build-browser/circuitpython.wasm` — browser board binary

**Important:** after changing config macros in `mpconfigboard.h` or
variant headers, do a clean rebuild (`rm -rf build-<variant>`).
Incremental builds can miss header changes.

### Debug builds

    $ make DEBUG=1             # assertions + debug symbols, -Og
    $ make STRIP=              # keep symbols, full optimization

Running
-------

### Node.js (standard variant)

    $ node run ./build-standard/circuitpython
    >>> import sys; print(sys.platform)
    wasi

Use `Ctrl-D` to exit.

### Node.js (browser variant)

    $ node --input-type=module -e "
    import { CircuitPython } from './js/circuitpython.mjs';
    const cp = await CircuitPython.create({
        wasmUrl: 'build-browser/circuitpython.wasm',
        onStdout: t => process.stdout.write(t),
    });
    "

### Browser

Serve the port directory and open `index.html`:

    $ python3 -m http.server 8080
    # open http://localhost:8080/index.html

The browser UI provides a code editor, display canvas, board
visualization with pin state, a sensor panel for analog input,
and NeoPixel rendering.

### Package manager

The built-in `fwip` package manager fetches libraries from the
[Adafruit CircuitPython Bundle](https://github.com/adafruit/Adafruit_CircuitPython_Bundle):

    >>> import fwip
    >>> fwip.install("display_text")

In browsers, installed packages persist in IndexedDB. In Node.js,
they are written to the WASI filesystem.

Testing
-------

    $ node test_standard.mjs           # standard variant tests
    $ node test_node.mjs               # Node.js integration tests
    $ node test_node_board.mjs         # board lifecycle tests (Node)
    $ node test_browser.mjs            # browser variant lifecycle tests

Filter tests:

    $ node test_browser.mjs --filter repl --verbose

Architecture
------------

### Key files

| File | Purpose |
|------|---------|
| `mpconfigboard.h` | CIRCUITPY_* feature flags |
| `supervisor/supervisor.c` | cp_init / cp_step / cp_exec / cp_ctrl_c/d, main() |
| `supervisor/port_memory.h` | port_memory_t — single struct owns all port state |
| `supervisor/vm_yield.c` | cooperative yield + SUSPEND mechanism |
| `supervisor/compile.c` | unified compile service (string + file) |
| `js/circuitpython.mjs` | JS API: CircuitPython.create(), lifecycle, contexts |
| `js/wasi-memfs.js` | in-memory WASI runtime + IndexedDB persistence |
| `js/semihosting.js` | JS↔WASM FFI: event ring + linear memory state export |
| `js/board-adapter.mjs` | pin name ↔ GPIO index bridge for visual renderers |
| `common-hal/board/board_pins.c` | mutable board dict (dynamic board definition) |
| `boards/wasm_browser/definition.json` | default board pin layout |

### Execution model

The port uses cooperative multitasking within a single WASM instance:

1. **C supervisor** (`cp_step`) runs per frame with a ~13ms wall-clock
   budget. The VM yields at backward branches when the budget expires.
2. **JS frame loop** calls `wasm_frame()` each animation frame. C
   requests the next frame via a WASM import (`port.requestFrame`).
3. **SUSPEND mechanism** allows the VM to pause mid-execution (for
   `time.sleep`, I/O wait, etc.) and resume on the next frame without
   blocking the browser's event loop.

### Hardware simulation

Virtual hardware state lives in WASM linear memory as flat arrays
(GPIO direction/value, analog values, PWM duty cycles, NeoPixel
buffers). JS reads/writes these via exported pointers — no fd I/O
in the hot path.

The board module uses `CIRCUITPY_MUTABLE_BOARD`: pins are compiled
into a mutable dict matching `definition.json` by default. For board
switching at runtime, JS can call `board_reset()` + `board_add_pin()`
+ `board_finalize()` with a different definition.

### Filesystem

The port uses WASI POSIX filesystem calls (`fd_read`, `fd_write`,
etc.) backed by an in-memory filesystem (`wasi-memfs.js`). The
`/CIRCUITPY/` drive is optionally persisted to IndexedDB across
browser page reloads. No block device or FAT filesystem is used.
