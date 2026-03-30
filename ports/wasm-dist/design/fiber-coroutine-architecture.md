# Fiber/Coroutine Architecture

## Overview

Split the monolithic 704K WASM binary into cooperating pieces:

```
┌─────────────────────────────────────────────────────┐
│  Main Thread                                        │
│  ┌───────────┐  ┌───────────┐  ┌────────────────┐  │
│  │ display.js│  │   hal.js  │  │  board-ui.js   │  │
│  │ (canvas)  │  │ (GPIO,PWM)│  │ (SVG, controls)│  │
│  └─────┬─────┘  └─────┬─────┘  └───────┬────────┘  │
│        │              │                 │            │
│        └──────────────┼─────────────────┘            │
│                       │ transferable ArrayBuffers    │
│                       ▼                              │
├───────────── postMessage ────────────────────────────┤
│                       │                              │
│  Worker               ▼                              │
│  ┌─────────────────────────────────────────────┐     │
│  │            Shared MEMFS (JS Map)            │     │
│  │   /sys/call  /sys/result  /sys/events       │     │
│  │   /sys/state /hal/*  /CIRCUITPY/*           │     │
│  ├─────────────────────────────────────────────┤     │
│  │                                             │     │
│  │  ┌──────────────┐    ┌────────────────┐     │     │
│  │  │supervisor.wasm│    │  fiber.wasm    │     │     │
│  │  │  scheduling  │───▶│  bytecode VM   │     │     │
│  │  │  semihosting │    │  pystack       │     │     │
│  │  │  compiler    │    │  yield/resume  │     │     │
│  │  └──────────────┘    └────────────────┘     │     │
│  │                       ▲  can pool 2-3       │     │
│  │                       │  instances          │     │
│  └───────────────────────┼─────────────────────┘     │
│                          │                           │
│  fiber instances share   │                           │
│  MEMFS Map, each has     │                           │
│  own pystack region      │                           │
└──────────────────────────────────────────────────────┘
```

## Three Binaries

### supervisor.wasm (~50-80K)
- Scheduling loop: decides which fiber runs next
- Semihosting dispatch: reads /sys/call, coordinates with JS
- Compiler (MICROPY_ENABLE_COMPILER=1): compiles .py → .mpy
- MEMFS coordination: manages /sys/ endpoints
- Thread lifecycle: mp_thread_init/deinit as fiber envelope

### fiber.wasm (~250-300K)
- Bytecode interpreter: vm.c + pystack + gc
- WASI fd access: read/write MEMFS via standard POSIX calls
- MICROPY_ENABLE_COMPILER=0: runs .mpy only
- Yield machinery: MICROPY_VM_YIELD_ENABLED + stackless
- Stateless template: state lives in pystack region of MEMFS

### JS Hardware Modules
- display.js: reads framebuffer from MEMFS, paints canvas
- hal.js: manages /hal/ endpoints, simulates GPIO/ADC/PWM
- board-ui.js: loads board SVG, makes pins interactive
  - LEDs light up, buttons are pressable, pins are toggleable
  - Board selection from settings.toml or dropdown

## Fiber as Coroutine

A fiber instance = cached module + .mpy blob + pystack region.

```js
// Creating a fiber
const fiber = await pool.acquire();          // from warm pool
wasi.writeFile('/fiber/0/code.mpy', mpy);    // bytecode blob
fiber.exports.cp_init();                      // point at MEMFS

// Execution sequence per frame
supervisor.exports.cp_step();                 // scheduling decisions
fiber0.exports.cp_step();                     // REPL
fiber1.exports.cp_step();                     // code.py
supervisor.exports.cp_step();                 // post-frame bookkeeping
```

Each fiber yields via pystack when:
- Wall-clock budget exhausted (YIELD_BUDGET)
- Waiting for I/O (YIELD_IO_WAIT)
- Sleeping (YIELD_SLEEP)
- Requesting display refresh (YIELD_SHOW)

The supervisor reads yield reason + arg from /sys/state and decides
whether to resume the same fiber or switch to another.

## Execution Model

"I go, 0 go, 1 go, I go, 2 go" — the supervisor orchestrates:

```
frame 1:  supervisor → fiber0 (REPL)                    → supervisor
frame 2:  supervisor → fiber1 (code.py)                  → supervisor
frame 3:  supervisor → fiber1 (code.py) → fiber2 (alarm) → supervisor
```

"Depth" is just another fiber in the sequence.  No special concept —
each fiber has its own pystack, all share MEMFS.

## Inter-Context Communication

### Intra-worker (supervisor ↔ fibers): direct linear memory for hot path
Per-frame communication between supervisor and fibers MUST NOT use WASI fd
operations. WASI fd calls (fd_write, fd_read, fd_seek) corrupt WASM linear
memory when memory.grow() detaches memory.buffer mid-frame, causing JS fd
handlers to read/write through stale DataView/Uint8Array references.

Instead, per-frame data uses exported C pointers for direct linear memory access:
- `sh_state_addr()` — supervisor/fiber exports VM state; JS reads directly
- `sh_event_ring_addr()` — JS writes events directly into a ring buffer
- Ring buffer layout: `[write_idx:u32] [read_idx:u32] [entries: N * sh_event_t]`

JS re-creates its DataView from memory.buffer at the top of each frame.
The shared MEMFS Map and /sys/ fd endpoints remain valid for init-time setup
and low-frequency semihosting calls (fetch, timer, persist).

### Main thread ↔ worker: transferable ArrayBuffers
```js
// Main thread: user toggled GPIO D3
const buf = new ArrayBuffer(8);
new DataView(buf).setUint8(0, 3);   // pin D3
new DataView(buf).setUint8(1, 1);   // HIGH
worker.postMessage({ path: '/hal/gpio', data: buf }, [buf]);

// Worker: received transfer, write to MEMFS
onmessage = (e) => {
    wasi.writeFile(e.data.path, new Uint8Array(e.data.data));
};
```

Zero-copy, no SharedArrayBuffer.

## Minimal Fiber Build

### Essential py/*.c (~37 files)
vm.c, gc.c, pystack.c, mpstate.c, runtime.c, malloc.c,
obj.c, objtype.c, objfun.c, objstr.c, objint.c, objfloat.c,
objlist.c, objtuple.c, objdict.c, objexcept.c, objclosure.c,
nlrsetjmp.c, qstr.c, vstr.c, map.c, sequence.c, mpz.c,
persistentcode.c (.mpy loading), ...

### Key config (mpconfigvariant.h)
```c
#define MICROPY_ENABLE_GC           1
#define MICROPY_ENABLE_PYSTACK      1
#define MICROPY_STACKLESS           1
#define MICROPY_VM_YIELD_ENABLED    1
#define MICROPY_ENABLE_COMPILER     0  // .mpy only
#define MICROPY_EMIT_NATIVE         0  // bytecode is WASM
#define MICROPY_VFS_POSIX           1  // WASI fd access
```

### What gets cut
- Compiler (lexer, parse, compile, scope, emitbc) — ~60K savings
- All native emitters — ARM, x64, thumb, etc.
- All display/displayio, common-hal
- All supervisor/shared (supervisor is its own binary)
- Frozen libraries
- Optional modules (available via supervisor or JS)

## Streaming

- fiber.wasm can be loaded via WebAssembly.compileStreaming()
- .mpy bytecode can be fetched as a stream
- Data between contexts flows as ReadableStream — fits fd model
- Supervisor could stream-compile .py → .mpy → fiber on the fly
