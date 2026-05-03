# WASI Platform Model

## Premise

The WASM port runs on a real platform: wasi-sdk provides POSIX-like
syscalls, the browser provides async I/O and rendering, and WASM
linear memory provides shared addressable storage.  This platform
has its own characteristics — it is not a microcontroller and it
is not unix.  The port should be built FOR this platform, using
its native primitives, rather than fighting it with MCU patterns
(interrupt-driven sleep) or unix patterns (blocking I/O).

## The platform we have

### wasi-sdk (sys)

POSIX-like syscall interface:
- `fd_read` / `fd_write` — file descriptor I/O
- `fd_seek` / `fd_close` — file management
- `clock_time_get` — monotonic + wall clock
- `poll_oneoff` — event polling with timeout
- `path_open` / `path_unlink_file` — filesystem
- `environ_get` — environment variables
- `proc_exit` — clean shutdown

These syscalls are the boundary between WASM and JS.  The WASI
shim in JS implements each one.  This is our `sys` layer.

### Browser/JS (os)

The host runtime provides:
- **Event loop** — rAF, setTimeout, postMessage, Promises
- **DOM** — canvas, iframe, input elements
- **Web APIs** — WebSerial, WebUSB, WebBLE, IndexedDB, fetch
- **Workers** — background threads with postMessage
- **SharedArrayBuffer** — cross-thread shared memory (with headers)

This is our `os` layer.  It manages processes (Workers), schedules
work (rAF), and provides I/O devices (serial ports, displays).

### WASM linear memory (ram)

A flat byte array addressable by both C and JS:
- **port_mem** — GC heap, pystack, serial rings, GPIO/analog/neopixel
  slots, all MEMFS-registered so JS can read/write directly
- **Static data** — framebuffer, cursor info, pin tables
- **Code** — compiled bytecode lives in GC heap (MEMFS-registered)

This is shared RAM.  The sync bus regions are views into it.

### Emscripten hardware (hal)

The iframe hardware renderer provides:
- **GPIO** — pin state, button interaction
- **Display** — SDL2 canvas rendering
- **NeoPixel** — LED strip visualization
- **Sensors** — simulated peripherals responding to bus transactions

This is our `hal` layer.  Swappable per board definition.

## The native interface: file descriptors

Everything is an fd.  The WASI shim maps fds to sync bus regions,
JS callbacks, or MEMFS files.

```
fd 0  = stdin       serial_rx ring (keystrokes, REPL input)
fd 1  = stdout      serial_tx ring (print output, REPL)
fd 2  = stderr      diagnostics (supervisor debug)
fd 3  = preopen     WASI filesystem root (MEMFS + IDB)
fd 4  = protocol    WS protocol messages (JSON lines)
fd 5+ = dynamic     opened files, bus channels
```

### I/O through fds

When Python does `i2c.writeto(0x77, data)`, the common-hal could:
1. Write the transaction to an I2C fd
2. The WASI shim receives `fd_write(i2c_fd, ...)`
3. Routes to the sync bus I2C region
4. A sensor panel or WebSerial bridge responds
5. Response appears as readable data via `fd_read(i2c_fd, ...)`

This uses the fd abstraction that WASI already provides, rather
than inventing a separate FFI or event ring mechanism.

### Sleep through poll_oneoff

`time.sleep(0.1)` becomes `poll_oneoff` with a clock subscription:

```c
// WASI poll_oneoff: "wake me when any of these events fire"
__wasi_subscription_t subs[2] = {
    { .u.tag = __WASI_EVENTTYPE_CLOCK,
      .u.u.clock = { .timeout = 100_000_000 } },  // 100ms
    { .u.tag = __WASI_EVENTTYPE_FD_READ,
      .u.u.fd_read = { .fd = event_fd } },         // wake on event
};
__wasi_event_t events[2];
size_t nevents;
__wasi_poll_oneoff(subs, events, 2, &nevents);
```

The WASI shim implements `poll_oneoff` as:
- Check if any events are pending (GPIO change, keystroke)
- If yes: return immediately with the event
- If no: return after timeout

No blocking.  No abort-resume.  Just a syscall that checks state
and returns.  The VM continues from where it left off — `poll_oneoff`
returned, `time.sleep` returns, the next bytecode executes.

In the main-thread model, `poll_oneoff` returns immediately (can't
block).  In the Worker model, it could use `Atomics.wait` for actual
sleep.  Same Python code, same WASI call, different shim behavior.

## Heap-based execution

The VM already runs from heap, not stack:

- **code_state** — bytecode execution state, lives on pystack
  (in port_mem, MEMFS-registered)
- **GC heap** — Python objects, also in port_mem
- **C stack** — just scaffolding for `mp_execute_bytecode`, rebuilt
  on every call, holds no persistent state

The generator protocol exploits this:

```
gen = compile_to_generator(user_code)
# Each vm_step():
result = gen.__next__()  # runs until yield/return/exception
# code_state persists on heap between calls
# C stack is gone — rebuilt next call
```

This IS the reactor pattern.  The platform calls `vm_step()`, the
VM does one chunk of work (one loop iteration, one import, one
REPL expression), yields, and the platform gets control back.

### What yields look like

- `time.sleep(n)` → yield with wakeup deadline
- `input()` → yield waiting for serial_rx data
- `i2c.readfrom_into()` → yield waiting for bus response
- End of loop body → natural yield (HOOK_LOOP)
- `await` in async code → asyncio yield

Each yield saves ip/sp to code_state (TRACE_TICK ensures they're
always current) and returns to the platform.

## asyncio as native scheduler

On MCU boards, asyncio is an optional library.  On our platform,
it should be the native execution model:

```python
# ctx0 — platform runtime (frozen, always active)
import asyncio

class Platform:
    def __init__(self):
        self.loop = asyncio.get_event_loop()
        self._js_promises = []

    async def run_user_code(self, path):
        code = compile(open(path).read(), path, 'exec')
        task = self.loop.create_task(self._exec(code))
        await task

    async def _exec(self, code):
        # User code runs as a task
        # time.sleep becomes await asyncio.sleep
        # I/O operations become awaitable
        exec(code, self._make_globals())

    async def run_repl(self):
        while True:
            line = await self.async_input(">>> ")
            try:
                result = eval(line, self._globals)
                if result is not None:
                    print(result)
            except SyntaxError:
                exec(line, self._globals)

    async def handle_js_promise(self, promise_id, result):
        # Called when a JS promise resolves
        # Delivers result to the waiting Python task
        future = self._pending_futures[promise_id]
        future.set_result(result)

# The event loop IS the frame loop:
# Each rAF: platform.loop.run_once()
# This runs ready tasks, checks timeouts, returns to JS
```

### Benefits

1. **No blocking**: every long operation is `await`. The event loop
   returns to JS between awaits.

2. **JS promises map to Python awaitables**: when Python needs data
   from JS (DOM read, fetch, WebSerial), it awaits.  The event loop
   runs other tasks while waiting.  When JS resolves the promise,
   the Python task resumes.

3. **time.sleep is just asyncio.sleep**: the event loop checks the
   deadline each time it runs.  No abort, no WFE, no latch.

4. **Background tasks are just tasks**: a sensor poller, a display
   refresher, a serial drainer — all asyncio tasks running in the
   same event loop.

5. **User code doesn't change**: `time.sleep(0.1)` works.
   `import board` works.  The platform translates these into
   asyncio operations internally.

## How modules adapt

### time.sleep → asyncio.sleep

```python
# frozen time.py (platform override)
import _time  # C module
import asyncio

def sleep(seconds):
    # If running in an asyncio task, yield properly
    loop = asyncio.get_event_loop()
    if loop.is_running():
        # Create a future that resolves after `seconds`
        awaitable = asyncio.sleep(seconds)
        # Run the awaitable synchronously from the task's perspective
        loop._run_until_complete(awaitable)
    else:
        _time.sleep(seconds)  # fallback to C implementation
```

### I2C → fd-based with async response

```python
# frozen busio.py (ws-worker variant)
class I2C:
    def writeto_then_readfrom(self, addr, out_buf, in_buf, ...):
        # Write transaction request to I2C fd
        os.write(self._fd, encode_transaction(addr, out_buf))
        # Read response (WASI shim provides it from sync bus)
        data = os.read(self._fd, len(in_buf))
        in_buf[:] = data
```

### JS FFI → promise-based

```python
# Using jsffi in async context
async def get_element():
    el = await js.document.getElementById("canvas")
    return el

# The jsffi proxy returns a Promise-wrapping awaitable
# The event loop handles the postMessage round-trip
```

## Frame loop integration

The platform's frame loop drives everything:

```
Each rAF (16ms):
  1. JS checks bus for pending events (GPIO, serial, sensor)
  2. JS calls vm_step():
     a. asyncio.loop.run_once()
     b. Ready tasks run (user code, REPL, background)
     c. Tasks yield at sleep/I/O/await points
     d. Loop returns — JS gets control back
  3. JS drains serial_tx → display
  4. JS reads GPIO/NeoPixel → sends to iframe
  5. JS renders framebuffer → canvas
  6. JS ticks sensor panels → writes bus analog slots
```

Step 2 takes as much time as the ready tasks need.  If all tasks
are sleeping, it returns immediately.  If user code is computing,
it runs until the next yield point (TRACE_TICK ensures this is
at most a few opcodes).

No frame budget enforcement needed — the generator/asyncio model
naturally yields.  No abort-resume — tasks yield cooperatively.
No sleep latches — asyncio.sleep handles timing correctly.

## What this replaces

| Old mechanism | New equivalent |
|---|---|
| abort-resume (nlr_jump_abort) | Generator yield at suspend points |
| vm_yield (upstream py/vm.c changes) | Standard generator protocol |
| Frame budget (8ms/10ms deadline) | Natural yield at TRACE_TICK points |
| wasm_wfe + sleep deadline | asyncio.sleep + poll_oneoff |
| HOOK_LOOP ip/sp saves | TRACE_TICK (always current) |
| semihosting event ring | fd-based I/O through WASI |
| chassis_frame state machine | asyncio event loop |
| C-level background callbacks | asyncio tasks |

## Implementation path

1. **poll_oneoff**: implement in WASI shim as event check + timeout
2. **TRACE_TICK override**: ip/sp always saved, any point is safe
   for yield
3. **Generator wrapping**: user code compiled as generator via
   lexer-directed compilation
4. **asyncio integration**: frozen asyncio that drives the event loop,
   `time.sleep` → `asyncio.sleep` bridge
5. **fd-based buses**: I2C/SPI/UART as file descriptors
6. **ctx0**: frozen platform runtime managing tasks

Each step is independently useful.  poll_oneoff alone fixes the
sleep problem.  TRACE_TICK alone fixes resume correctness.
Generator wrapping alone enables the reactor model.  asyncio
integration ties them together.

## The mental model

The WASM binary is a POSIX program.  It reads and writes fds.
It allocates from heap.  It calls poll_oneoff when it needs to
wait.  The JS runtime is its operating system — it implements
the syscalls, manages the display, handles I/O devices, and
calls the program's step function on a timer.

The simulation of a CircuitPython board is built ON this
platform, not IN SPITE OF it.
