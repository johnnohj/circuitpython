# WASM-WASI Port Architecture

## The Split-Port Model

This port is **two cooperating processes** that together present one
CircuitPython port.  The browser forces this split: Web Workers cannot
touch the DOM, and the main thread needs to remain responsive to the
browser event loop.

```
┌─────────────────────────────────────────────────────┐
│                    Browser Tab                       │
│                                                      │
│  ┌──────────────────────┐   ┌─────────────────────┐ │
│  │   Main Thread         │   │   Web Worker         │ │
│  │   (Reactor)           │   │   (Hardware)         │ │
│  │                       │   │                       │ │
│  │  IS the supervisor    │   │  IS the port hardware │ │
│  │  Runs user code       │   │  Owns C display       │ │
│  │  Manages lifecycle    │   │  Owns common-hal      │ │
│  │  Touches the DOM      │   │  Renders framebuffers │ │
│  │  Python hw shims      │   │  Reads/writes state   │ │
│  │                       │   │                       │ │
│  └──────────┬────────────┘   └──────────┬────────────┘ │
│             │                           │               │
│             └─────────┬─────────────────┘               │
│                       │                                  │
│              ┌────────▼────────┐                        │
│              │   OPFS Bus       │                        │
│              │  /hw/gpio/state  │                        │
│              │  /hw/display/fb  │                        │
│              │  /hw/repl/rx,tx  │                        │
│              │  /hw/control     │                        │
│              └─────────────────┘                        │
└─────────────────────────────────────────────────────────┘
```

## Who Owns What

### Main Thread Reactor — The Supervisor

The reactor IS CircuitPython's supervisor.  It:

- **Runs user code**: code.py, REPL input, `while True:` loops —
  all work because the VM yields after each bytecode budget, returning
  control to the browser event loop via `requestAnimationFrame`
- **Manages lifecycle**: safe mode, auto-reload, error display
- **Owns the browser UI**: DOM access, keyboard events, Canvas painting
- **Decides what to display**: status bar content, REPL banner, errors
- **Writes hardware requests**: Python shims (digitalio.py, etc.)
  write to OPFS

The reactor does NOT:
- Run C displayio (re-entrancy with host-driven stepping)
- Touch hardware state arrays directly (goes through OPFS)

### Web Worker — The Port Hardware

The worker IS the port's hardware layer.  It:

- **Owns the C display pipeline**: framebufferio, terminalio, fontio
- **Owns common-hal**: GPIO state arrays, I2C register files, etc.
- **Renders framebuffers**: composites TileGrid → RGB565 → OPFS
- **Processes hardware requests**: reads OPFS endpoints, updates state
- **Runs its own event loop**: can block, sleep, poll freely

The worker does NOT:
- Run user Python code (that's the reactor's job)
- Decide what the status bar says (that's the supervisor's job)
- Touch the DOM or browser APIs
- Import user modules or run code.py

### OPFS — The Bus

OPFS (Origin Private File System) is shared storage accessible from
both the main thread and Web Workers.  It replaces what would be
DMA, shared memory, or hardware registers on a real board.

All communication between reactor and worker goes through OPFS files.
This is intentionally simple: no SharedArrayBuffer, no postMessage
serialization, no complex IPC.  Just files with known binary formats.

## Data Flow

### REPL Text

```
Keyboard → JS event → POST /key → /hw/repl/rx
                                        │
                              Worker reads, feeds to
                              pyexec_event_repl_process_char()
                                        │
                              REPL output → mp_hal_stdout_tx_strn
                                        │
                              ┌─────────▼──────────┐
                              │  Terminal TileGrid   │
                              │  (scroll area)       │
                              └─────────┬────────────┘
                                        │
                              displayio compositor
                                        │
                              ┌─────────▼──────────┐
                              │  RGB565 Framebuffer  │
                              └─────────┬────────────┘
                                        │
                              write to /hw/display/fb
                                        │
                              Browser fetches, paints Canvas
```

### Hardware I/O

```
Reactor (user code):                    Worker:
  import digitalio                        hw_opfs_read_all()
  pin = DigitalInOut(board.D13)             ↓
  pin.value = True                        reads /hw/gpio/state
        │                                updates state arrays
  digitalio.py writes to                  runs common-hal logic
  /hw/gpio/state                          hw_opfs_flush_all()
                                            ↓
                                          writes /hw/gpio/state
                                          (with results, if any)
```

### Display

The worker renders; the supervisor decides what to render.

On real CircuitPython boards:
- `supervisor_status_bar_update()` sends OSC escape sequences via
  `serial_write()` → `board_serial_write_substring()` → terminal
- The Terminal's VT100 parser recognizes OSC and routes to status bar
- REPL text goes to the scroll area via `mp_hal_stdout_tx_strn()`
- Both paths converge at `common_hal_terminalio_terminal_write()`

In our port, the equivalent should be:
1. Reactor/supervisor writes status bar content to an OPFS endpoint
2. Worker reads it and feeds it to the Terminal (OSC → status bar)
3. Reactor's REPL output goes to an OPFS endpoint
4. Worker reads it and feeds it to the Terminal (text → scroll area)
5. Worker composites and flushes framebuffer

**Current shortcut**: The worker runs `pyexec_event_repl_init()` and
the event-driven REPL directly, keeping the entire text pipeline
in one address space.  This works but conflates supervisor and
hardware roles.  The REPL should eventually move to the reactor,
with text flowing through OPFS to the worker for rendering.

## Variant Summary

| Variant    | Role              | Entry Model  | Display      | Hardware     |
|------------|-------------------|------------- |------------- |------------- |
| standard   | Test suite / CLI  | _start block | None         | None         |
| reactor    | Browser supervisor| init/step    | Python shims | Python shims |
| worker     | Browser hardware  | _start block | C pipeline   | C common-hal |
| opfs       | Persistence       | _start block | None         | None         |

The **standard** variant runs the test suite and CLI.  No browser.

The **reactor** and **worker** variants work as a pair in the browser.
Neither is complete alone — together they present one CircuitPython port.

The **opfs** variant adds OPFS state checkpointing to the standard variant.

## OPFS Endpoint Reference

```
/hw/
├── control              32B   Signal/state/frame protocol
├── display/
│   ├── fb               W×H×2 RGB565 framebuffer (worker → browser)
│   └── scene            Variable  Scene description (reactor → worker)
├── repl/
│   ├── rx               Variable  Keyboard → worker (append-read)
│   └── tx               Variable  Worker output stream
├── gpio/
│   └── state            384B   64 × 6B pin entries
├── analog/
│   └── state            256B   64 × 4B pin entries
├── pwm/
│   └── state            512B   64 × 8B pin entries
├── neopixel/
│   └── data             Variable  [count][pin,len,pixels...]
└── i2c/
    └── dev/{addr}       256B   Register file per I2C address
```

## Two VMs, One Port

Python runs in **both** processes.  The reactor has a CircuitPython VM
(user code, Python hardware shims).  The worker has a separate
CircuitPython VM (worker.py, event-driven REPL).  Each has its own
heap, GC, and module state.  They cannot share Python objects.

This is not a bug — it's the architecture.  The two VMs communicate
exclusively through OPFS files.  A `struct.pack` in the reactor
produces the exact bytes that a C `read()` in the worker consumes.
No proxy objects, no shared stack, no interleaving.

### Where the REPL lives

Currently: the worker VM, because the terminal display is there.
Conceptually: the REPL is user interaction (supervisor territory).
The tension is real — the REPL needs the terminal, the terminal
needs the display, the display lives in the worker.

Moving the REPL to the reactor means text crosses OPFS to reach the
terminal.  One more hop, but cleaner separation.  This is a future
refactor, not a current blocker.

### Why OPFS over Emscripten-style bridging

The Emscripten port crossed language boundaries on every call:
Python → C → `EM_JS` → JavaScript → `JsProxy` → back.  Each crossing
had its own calling convention, memory model, and failure modes.
Re-entrancy was the killer — a JavaScript callback firing in the
middle of a C function that was called from Python.

OPFS collapses language boundaries into **data boundaries**.  Both
sides read and write the same binary formats.  The polling model
(read, process, write, sleep) is inherently sequential — you cannot
be re-entered by a file.  The trade-off is latency (16ms poll
intervals vs microsecond function calls), but for hardware simulation
in a browser, that's acceptable.

What we gain: each VM is a clean, self-contained CircuitPython
instance.  Neither needs to know about the other's internals.
The OPFS endpoint format is the only contract between them.

## Design Principles

1. **The port is both pieces working together.**  Neither the reactor
   alone nor the worker alone is a complete CircuitPython port.

2. **Internal wiring must not compromise external connection points.**
   Our port interacts with CircuitPython through common-hal functions,
   supervisor hooks, and mp_hal_* functions.  The OPFS bus is internal.

3. **The supervisor decides, the worker renders.**  The main thread
   controls what the user sees (banner, errors, status).  The worker
   is a display driver that receives text and produces pixels.

4. **OPFS is the bus, not the brain.**  Endpoints are typed binary
   blobs.  The protocol is read-write-poll, not request-response.
   You cannot be re-entered by a file.

5. **Both variants use circuitpy_mpconfig.h.**  This is a CircuitPython
   port, not a MicroPython port.  We follow CircuitPython conventions
   and use CircuitPython's module system.

6. **User code can `while True:` on the main thread.**  The reactor's
   step/yield model makes blocking user code transparent to the browser.
   The VM yields after each bytecode budget, returning control to the
   event loop.  Only the worker uses a traditional blocking poll loop.

7. **Two VMs is fine.**  Each process runs its own CircuitPython
   instance.  They share no Python state.  The OPFS endpoint format
   is the only contract between them.

## Key Insight: The Interpreter IS Already WASM

The bytecode interpreter (vm.c) compiles to WASM instructions.
When Python does `1 + 2`, the interpreter calls `mp_binary_op()`
— a WASM function calling another WASM function.  We don't need
to compile Python to new WASM modules.  We need to make the
existing interpreter use WASM's native features.

**`-fwasm-exceptions`** does exactly this.  It makes the C
setjmp/longjmp (used for NLR exception handling) compile to
native WASM try/catch/throw instructions.  Every `try/except`
in every Python file benefits — bytecoded, frozen, REPL —
without any native emission or new WASM modules.

The `py/asmwasm.c` infrastructure (exception handling, generators,
cooperative yield) remains available if runtime WASM compilation
becomes practical in the future.  For now, `MICROPY_EMIT_WASM=0`.

## Target Architecture

```
┌─────────────────────┐     ┌──────────────────────────┐
│   Main Thread        │     │   Web Worker              │
│                      │     │                            │
│  Light supervisor    │     │  Full CircuitPython VM     │
│  JS event loop       │     │  C display pipeline        │
│  DOM / Canvas        │     │  common-hal state           │
│  Keyboard input      │     │  Bytecode interpreter      │
│                      │     │  (-fwasm-exceptions for EH) │
└──────────┬───────────┘     └──────────┬─────────────────┘
           │                            │
           └──────────┬─────────────────┘
                      │
             IPC bus (deployment-dependent):
             ┌────────▼────────┐
             │  SharedArrayBuffer (if CORS headers present)
             │  OPFS files     (if CORS headers present)
             │  MEMFS / IDBFS  (no special headers needed)
             └─────────────────┘
```

### IPC bus options

All three use the same binary formats (binproto.h, hw_opfs
endpoint layouts).  The choice is deployment-dependent:

| Bus              | Headers needed  | Latency     | Persistence |
|------------------|-----------------|-------------|-------------|
| SharedArrayBuffer| COOP + COEP     | ~0 (shared) | No          |
| OPFS             | COOP + COEP     | ~1ms (I/O)  | Yes         |
| MEMFS / IDBFS    | None            | ~1ms        | IDBFS: yes  |

**If CORS headers are present**: OPFS sync access AND
SharedArrayBuffer are both available (same headers enable both).
SAB for hot paths (framebuffer, keyboard), OPFS for persistence.

**If no CORS headers**: MEMFS for IPC (in-memory, no persistence),
IDBFS for persistence (code.py, settings.toml).  Works on any
static host (GitHub Pages, S3, etc.).

### What runs where

**Main thread (light)**:
- JS supervisor loop (`requestAnimationFrame`)
- DOM event handling (keyboard, mouse, resize)
- Canvas painting (reads framebuffer from shared memory or fetch)
- Lifecycle management (auto-reload, safe mode)

**Worker (full-fat)**:
- Full CircuitPython VM (bytecode interpreter with WASM EH)
- C display pipeline (framebufferio, terminalio, fontio)
- common-hal implementations (GPIO, I2C, SPI state)
- Event-driven REPL or code.py execution
- `RUN_BACKGROUND_TASKS` = return to JS (cooperative yield)

### Convergence pieces

1. **binproto.h** — compact binary protocol for hardware operations
2. **Shared memory** — SAB or WASM linear memory for hot paths
3. **`-fwasm-exceptions`** — clean NLR for the interpreter, no sjlj

Persistence (code.py, settings.toml) via IDBFS (no headers) or
OPFS (with headers).  The IPC bus is internal — external connection
points (common-hal, mp_hal_*) remain the same regardless.
