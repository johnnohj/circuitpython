# WASI Platform Model — Implementation Plan

Companion to `wasi-platform-model.md`.  This document identifies
specific VM functions, patterns, and adaptation points for making
the platform model a reality.

## Survey findings

### Blocking points in the current codebase

| Function | Location | Behavior | Platform problem |
|---|---|---|---|
| `mp_hal_stdin_rx_chr` | supervisor/micropython.c | Loops forever until serial_rx has data | Blocks main thread if no input |
| `mp_hal_delay_ms` | port/vm_abort.c | Calls `wasm_wfe(ms)` → abort | Destroys C stack, requires resume |
| `mp_event_wait_ms` | py/scheduler.c | Calls `MICROPY_INTERNAL_WFE(timeout)` | Same as above via WFE macro |
| `mp_event_wait_indefinite` | py/scheduler.c | Calls `MICROPY_INTERNAL_WFE(-1)` | Infinite wait |
| `port_idle_until_interrupt` | port/vm_abort.c | Sets VM_ABORT_WFE, aborts | Same abort mechanism |
| `pyexec_file` | shared/runtime/pyexec.c | lex→parse→compile→exec, all synchronous | Blocks for entire file execution |
| `pyexec_friendly_repl` | shared/runtime/pyexec.c | Blocking REPL loop | Blocks forever |

### Non-blocking alternatives already in the VM

| Mechanism | Location | How it helps |
|---|---|---|
| `pyexec_event_repl_process_char(c)` | shared/runtime/pyexec.c | Process one REPL char at a time, non-blocking |
| `_run_iter()` | modules/asyncio/core.py | Run one event loop iteration, returns ms-to-next |
| Generator `__next__` | py/objgenerator.c | Execute until yield, save state, return |
| `mp_hal_stdio_poll` | supervisor/micropython.c | Non-blocking check for available input |
| `serial_bytes_available` | supervisor/shared/serial.c | Check serial_rx without blocking |
| `MICROPY_REPL_EVENT_DRIVEN` | already enabled | Event-driven REPL (char-at-a-time) |

### Platform primitives available

| Primitive | WASI/JS | Use |
|---|---|---|
| `poll_oneoff` | WASI syscall (stub) | Sleep with event wake |
| `fd_read/fd_write` | WASI syscall (working) | All I/O through fds |
| `clock_time_get` | WASI syscall (working) | Monotonic time |
| TRACE_TICK | py/vm.c (no-op without SETTRACE) | Per-opcode ip/sp save |
| `mp_execute_bytecode` | py/vm.c | Runs code_state from heap |
| `mp_compile` | py/compile.c | Source → bytecode function |

---

## Implementation phases

### Phase 1: poll_oneoff as the sleep primitive

**Goal**: Replace abort-based sleep with poll_oneoff.  The VM calls
a WASI syscall instead of aborting.  The shim checks for events
and returns.

**Changes needed**:

1. **WASI shim** (`js/wasi.js`): implement `poll_oneoff` properly

   ```js
   poll_oneoff(in_ptr, out_ptr, nsubscriptions, nevents_ptr) {
       const mem = self._view();
       let nevents = 0;
       for (let i = 0; i < nsubscriptions; i++) {
           const sub = parse_subscription(mem, in_ptr + i * 48);
           if (sub.type === EVENTTYPE_CLOCK) {
               // Check if timeout has elapsed since subscription
               // For main-thread: always return immediately (can't block)
               // For Worker: could Atomics.wait here
               write_event(mem, out_ptr + nevents * 32, sub, 0);
               nevents++;
           }
           if (sub.type === EVENTTYPE_FD_READ) {
               // Check if fd has data available
               if (fd_has_data(sub.fd)) {
                   write_event(mem, out_ptr + nevents * 32, sub, 0);
                   nevents++;
               }
           }
       }
       mem.setUint32(nevents_ptr, nevents, true);
       return ERRNO.SUCCESS;
   }
   ```

2. **mp_hal_delay_ms override** (`mpconfigport.h` or `vm_abort.c`):

   ```c
   void mp_hal_delay_ms(mp_uint_t ms) {
       // Use poll_oneoff: sleep for ms OR wake on event fd
       __wasi_subscription_t subs[2];
       // sub 0: clock timeout
       subs[0].u.tag = __WASI_EVENTTYPE_CLOCK;
       subs[0].u.u.clock.timeout = ms * 1000000ULL; // ns
       // sub 1: event fd (GPIO change, keystroke)
       subs[1].u.tag = __WASI_EVENTTYPE_FD_READ;
       subs[1].u.u.fd_read.file_descriptor = EVENT_FD;
       __wasi_event_t events[2];
       size_t nevents;
       __wasi_poll_oneoff(subs, events, 2, &nevents);
       // Returns immediately on main thread, may block on Worker
       mp_event_handle_nowait(); // drain pending events
   }
   ```

3. **Event fd**: designate an fd (e.g., fd 5) as the "event ready"
   channel.  When JS writes to it (button press, sensor update),
   poll_oneoff returns early.

**What this fixes**: Sleep no longer aborts the VM.  The C stack
persists.  `time.sleep(0.1)` calls poll_oneoff, which returns
(immediately on main thread, after timeout on Worker), and the VM
continues from where it left off.  No resume machinery needed.

**Risk**: On the main thread, poll_oneoff can't actually block.
It returns immediately.  For `time.sleep(0.1)`, this means the
VM runs without sleeping — it busy-loops through the sleep.
This is addressed by Phase 3 (generator wrapping), where the
sleep becomes a yield point.

**Mitigation**: In the main-thread model, poll_oneoff checks the
wall clock.  If the timeout hasn't elapsed, it returns a "not
ready" event.  The caller (`mp_hal_delay_ms`) sees no event and
must decide: loop again (busy-wait) or yield.  We add a yield
path: if poll_oneoff returned "not ready" and we're on the main
thread, set a flag that TRACE_TICK checks, causing the VM to
yield on the next opcode.

### Phase 2: TRACE_TICK for universal suspend

**Goal**: ip/sp are always current in code_state.  Any point in
execution is a valid suspend/resume point.

**Changes needed**:

1. **Override TRACE_TICK** (`mpconfigport.h`):

   ```c
   // Save ip/sp on every opcode dispatch.
   // Two stores per opcode — in WASM, these are i32.store to
   // port_mem (linear memory), negligible cost.
   #define MICROPY_PY_SYS_SETTRACE (0)  // don't enable full trace

   // Redefine TRACE_TICK without enabling SETTRACE infrastructure
   // This requires a small upstream-compatible change: extract the
   // TRACE_TICK definition from the #if MICROPY_PY_SYS_SETTRACE block
   // Or: use MICROPY_VM_HOOK_LOOP to save ip/sp (already there)
   ```

   **Problem**: TRACE_TICK is inside `#if MICROPY_PY_SYS_SETTRACE`.
   Without enabling SETTRACE, TRACE_TICK is `#define`d to nothing.
   Enabling SETTRACE pulls in the full profiling infrastructure
   (frames, callbacks, mp_prof_*).

   **Alternative A**: Save ip/sp in MICROPY_VM_HOOK_LOOP (already
   done — this is the current approach).  Limitation: only fires
   at backwards branches.

   **Alternative B**: Add ip/sp saves to DISPATCH() via a new hook:
   ```c
   #define MICROPY_VM_HOOK_DISPATCH \
       code_state->ip = ip; code_state->sp = sp;
   ```
   This would require a small upstream change (add the hook to
   the DISPATCH macro in vm.c).

   **Alternative C**: Enable SETTRACE but stub out the profiling:
   ```c
   #define MICROPY_PY_SYS_SETTRACE (1)
   // Stub the profiling functions to no-ops
   #define mp_prof_instr_tick(cs, exc) do {} while(0)
   ```
   TRACE_TICK would fire but call our stubs.  ip/sp still get
   saved because TRACE_TICK receives them as arguments — but
   examining the actual TRACE_TICK macro, it doesn't save them
   to code_state.  It calls `mp_prof_instr_tick`.  So this
   doesn't help.

   **Alternative D (recommended)**: Our HOOK_LOOP already saves ip/sp.
   For Phase 2, accept the backwards-branch-only limitation.
   Address linear code in Phase 3 (generator wrapping at natural
   boundaries).  HOOK_RETURN already runs background tasks.
   Together: ip/sp saved at loop boundaries, background tasks at
   function returns.  This covers most real code.

**What this fixes**: Resume correctness for abort-resume (current
model) or yield-resume (future model).

### Phase 3: Generator wrapping for reactor execution

**Goal**: User code runs as a generator.  Each `vm_step()` call
runs one chunk and yields.  The platform calls `vm_step()` per
frame.

**Approaches**:

#### 3A: Lexer-directed yield insertion

The lexer/compiler inserts yield points at detected boundaries:

```python
# User wrote:
while True:
    pixels[0] = next_color()
    time.sleep(0.1)

# Compiler transforms to (conceptually):
def _user_code():
    while True:
        pixels[0] = next_color()
        yield  # <-- inserted at time.sleep
        time.sleep(0.1)
        yield  # <-- inserted at loop boundary
```

**Mechanism**: Add a compile flag that inserts `YIELD_VALUE`
opcodes at specific points:
- After `time.sleep` / `time.sleep_ms` calls
- At `while True:` loop boundaries (already detected by HOOK_LOOP)
- At explicit `yield` in user code (already works)

**Complexity**: Moderate.  Requires changes to `py/compile.c` to
detect sleep calls and insert yields.  The detection is pattern-
based (function name matching), which is fragile.

#### 3B: time.sleep as asyncio.sleep (frozen wrapper)

Override `time.sleep` in frozen Python:

```python
# frozen time.py
import _time
import asyncio

def sleep(seconds):
    # If in an asyncio context, yield properly
    if asyncio._running:
        asyncio.sleep(seconds).__next__()  # drive the coroutine
    else:
        _time.sleep(seconds)  # fallback
```

**Problem**: `asyncio.sleep` returns a coroutine.  Calling
`__next__` on it schedules the sleep but doesn't yield the
calling code.  The caller needs to be `async` for `await` to
work.  User code isn't async.

**Workaround**: Use `_run_iter()` inside `time.sleep`:

```python
def sleep(seconds):
    deadline = _time.monotonic() + seconds
    while _time.monotonic() < deadline:
        dt = _run_iter()  # run any pending asyncio tasks
        if dt < 0 or dt > remaining_ms:
            # No tasks or next task is after our deadline
            poll_oneoff(remaining_ms)  # WASI sleep
            break
```

This makes `time.sleep` cooperative: it runs the asyncio event
loop while waiting.  On the main thread, poll_oneoff returns
immediately, and we need the generator wrapping (3A) or budget
yield (Phase 2) to actually return to JS.

#### 3C: Wrap user code in asyncio.Task (ctx0 approach)

```python
# ctx0 platform runtime
async def run_user_code(path):
    code = compile(open(path).read(), path, 'exec')
    # Execute in a task context
    await exec_as_async(code)

async def exec_as_async(code):
    # This is the hard part — how to make synchronous user code
    # behave as async.  Options:
    # a) Transform the code (insert awaits) — fragile
    # b) Run in a sub-interpreter with yield hooks — complex
    # c) Use TRACE_TICK + flag to yield back to event loop — best
    pass
```

**The TRACE_TICK + flag approach (3C-c)**:

```c
// In the event loop's vm_step:
// 1. Set a "yield after N opcodes" counter
// 2. TRACE_TICK decrements the counter
// 3. When counter hits 0, save ip/sp, return MP_VM_RETURN_YIELD
// 4. Event loop gets control, does JS work, calls vm_step again
```

This is essentially a time-slicing scheduler implemented via
TRACE_TICK.  The counter is the "budget" but expressed as opcode
count, not wall time.  The advantage: no C stack destruction
(just a clean return from mp_execute_bytecode with YIELD).

**Requires**: A new return kind or repurposing MP_VM_RETURN_YIELD
for non-generator contexts.  Currently, YIELD is only valid for
generator functions.  Using it for top-level code requires
changes to `mp_execute_bytecode` or wrapping user code in a
generator implicitly.

### Phase 4: asyncio integration

**Goal**: asyncio's event loop IS the frame loop.  JS calls
`asyncio_step()` per rAF.  All Python execution is tasks.

**Changes needed**:

1. **Expose `_run_iter` as the frame step**:

   ```python
   # Already exists in core.py:
   def _run_iter():
       """Run one iteration. Returns ms-to-next or -1."""
   ```

   The platform calls this per frame.  If it returns > 0, there's
   a sleeping task — JS doesn't need to call again until then
   (but can for responsiveness).  If -1, no tasks — idle.

2. **Bridge JS promises to asyncio awaitables**:

   ```python
   # When Python needs JS (jsffi call):
   async def call_js(ref, method, *args):
       future = asyncio.Future()
       promise_id = _register_pending(future)
       os.write(PROMISE_FD, encode_request(promise_id, ref, method, args))
       return await future

   # When JS resolves the promise:
   # Worker receives result, writes to PROMISE_FD
   # _run_iter sees readable data on PROMISE_FD
   # Resolves the future, resumes the waiting task
   ```

3. **Background tasks as asyncio tasks**:

   ```python
   # Instead of C-level background_callback_run_all:
   async def display_refresh():
       while True:
           displayio_background()
           await asyncio.sleep(0.016)  # ~60fps

   async def sensor_poll():
       while True:
           read_sensors()
           await asyncio.sleep(0.1)
   ```

**Benefit**: Unified scheduling.  No separate C-level background
callback system.  Everything is a Python task.  The event loop
manages priorities.

### Phase 5: fd-based buses

**Goal**: I2C, SPI, UART transactions go through file descriptors.
The WASI shim routes them.

**Changes needed**:

1. **Open bus fds at init**:
   ```c
   int i2c_fd = open("/dev/i2c/0", O_RDWR);  // WASI path_open
   ```
   The WASI shim intercepts `/dev/i2c/*` paths and creates bus
   channel fds.

2. **Common-hal writes transactions**:
   ```c
   void common_hal_busio_i2c_writeto(busio_i2c_obj_t *self,
       uint16_t addr, const uint8_t *data, size_t len, bool stop) {
       // Encode as bus transaction
       uint8_t header[4] = { addr >> 8, addr & 0xFF, len >> 8, len & 0xFF };
       write(self->fd, header, 4);
       write(self->fd, data, len);
       // Response comes back on next read
   }
   ```

3. **WASI shim routes to sync bus**:
   ```js
   fd_write(fd, iovs, ...) {
       if (busChannels.has(fd)) {
           const ch = busChannels.get(fd);
           ch.port.push(ch.region, data);
           // Sensor panel or bridge responds
       }
   }
   ```

**Benefit**: No special FFI for bus I/O.  Standard fd_read/fd_write.
Sensor panels and WebSerial bridges attach to the sync bus regions.
The VM doesn't know who's on the other end.

### Phase 6: ctx0 platform runtime

**Goal**: Frozen Python manages the lifecycle.  The C port just
provides primitives.  Python owns scheduling, code execution,
and the REPL.

This phase builds on all previous phases.  See
`ctx0-supervisor-context.md` for the full vision.

---

## Adaptation point summary

| Component | Current | Platform model |
|---|---|---|
| `mp_hal_delay_ms` | `wasm_wfe()` → abort | `poll_oneoff()` → return |
| `mp_hal_stdin_rx_chr` | Busy loop + abort | `poll_oneoff(serial_rx_fd)` |
| `MICROPY_INTERNAL_WFE` | `wasm_wfe()` → abort | `poll_oneoff()` |
| `time.sleep` | abort-resume | asyncio.sleep / poll_oneoff |
| `TRACE_TICK` | no-op | ip/sp save (via HOOK_LOOP or custom) |
| `pyexec_file` | blocking lex→parse→compile→exec | Generator-wrapped or asyncio task |
| `_run_iter` | asyncio internals | Platform frame step |
| `background_callback_run_all` | C callback queue | asyncio tasks |
| I2C/SPI/UART common-hal | port_mem direct | fd-based via WASI |
| Lifecycle (boot→code→REPL) | C state machine | ctx0 Python runtime |

## Recommended implementation order

1. **poll_oneoff** — immediate value, fixes sleep without abort
2. **HOOK_LOOP ip/sp** — already done, accept limitation for now
3. **time.sleep wrapper** — frozen Python, cooperative with asyncio
4. **_run_iter as frame step** — expose to platform, replace chassis_frame
5. **fd-based buses** — after sleep and scheduling work
6. **ctx0** — after everything else is stable

Each phase is independently deployable and testable.
