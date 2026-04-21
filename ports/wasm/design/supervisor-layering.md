# Supervisor Layering — JS / Port / Supervisor / VM

## The Interrupt Chain

CircuitPython has a natural layering where each outer layer can interrupt
the one inside it. On real hardware:

```
Hardware (ISR)  →  Port  →  Supervisor  →  VM
```

On WASM, JS replaces hardware:

```
JS (host)  →  Port/WASM  →  Supervisor  →  VM
```

Each layer can interrupt inward:
- **JS interrupts port**: submits events to semihosting ring, drives timing
- **Port interrupts supervisor**: `sh_on_event` routes Ctrl-C, HW changes, wake signals
- **Supervisor interrupts VM**: `mp_vm_request_yield`, budget check via `wasm_vm_hook_loop`

Information flows back outward:
- **VM → supervisor**: yield reason, line number, completion status
- **Supervisor → port**: `sh_export_state`, `sh_update_trace`, bg_pending flag
- **Port → JS**: linear memory reads (semihosting state, framebuffer, HAL endpoints)

## Layer Responsibilities

### JS (host) — `circuitpython.mjs`, context managers

JS is the hardware. It owns:
- Time (rAF, setTimeout, performance.now)
- User input (keyboard, mouse, touch)
- Display output (canvas, DOM)
- External I/O (WebUSB, WebSerial, WebBluetooth)
- Persistence (IndexedDB)

JS communicates with C exclusively through:
- **Semihosting event ring** (JS → C): typed events for input, timers, hardware
- **WASM exports** (JS → C): `cp_step`, `cp_hw_step`, `cp_exec`, etc.
- **Linear memory reads** (C → JS): semihosting state, framebuffer, HAL endpoints

JS never touches the VM directly. It doesn't know about bytecode, pystack,
or code_state. It knows about "state" (ready/executing/suspended), "line
number" (for coordination), and "events" (what happened).

### Port/WASM — `port.c`, `hal.c`, `semihosting.c`

The port is the boundary between JS and CircuitPython. It translates
between JS's event-driven world and the supervisor's tick-driven world.

The port owns:
- **Virtual hardware state**: HAL fd endpoints (`/hal/gpio`, `/hal/neopixel`, etc.)
- **Event routing**: `sh_on_event` dispatches JS events to the right handler
- **Hardware stepping**: `cp_hw_step` drives HAL polling, tick catchup, display refresh
- **Background work signaling**: `port_wake_main_task` → `sh_set_bg_pending`

Key principle: the port controls the VH (virtual hardware). JS tells the port
what happened (button pressed, sensor data arrived), and the port updates the
VH state. The supervisor sees the updated VH state on its next step. Hardware
updates fire independently of the VM — it doesn't matter if the VM is sleeping.

### Supervisor — `supervisor.c`, `context.c`, `compile.c`

The supervisor coordinates between the port/VH and the Python VM. It doesn't
know about JS directly — it talks to semihosting for state export and to the
port for hardware state.

The supervisor owns:
- **Lifecycle state machine**: READY / EXECUTING / SUSPENDED
- **Context scheduling**: which Python context runs next, for how long
- **Compilation**: source → bytecode → code_state
- **Event-driven wake**: matching semihosting events to sleeping contexts
- **Frame budget**: how much VM time per step

The supervisor can suspend the VM at any time (budget expired, Ctrl-C,
hardware event that needs immediate attention). The VM doesn't know why
it was suspended — it just yields and gets resumed later.

### VM — `vm_yield.c`, `py/vm.c`

The Python VM executes bytecode. It yields at natural points (backwards
branches, function calls) when the supervisor requests it. It doesn't
know about JS, the port, or the hardware — it just runs Python code and
yields when told to.

The VM communicates outward via:
- **Yield reason**: why did I stop? (budget, sleep, I/O)
- **Line number**: where am I? (extracted at backwards branches)
- **HAL reads/writes**: Python `digitalio.DigitalInOut` → common-hal → HAL fd

## Port Interface Mapping

Traditional CircuitPython port functions and their WASM meanings:

| Function | Real Hardware | WASM |
|---|---|---|
| `port_wake_main_task` | Unblock RTOS main task | Set bg_pending flag — port has new state for supervisor |
| `port_task_yield` | Pause CP, let other RTOS tasks run | Frame done, return control to JS |
| `port_idle_until_interrupt` | CPU sleep until ISR | Nothing to do — JS can throttle to slower timer |
| `port_background_task` | Poll USB, check buttons | Call `supervisor_tick` (queue background work) |
| `port_background_tick` | Port-specific periodic work | Currently no-op; could drive VH-specific periodic tasks |
| `port_interrupt_after_ticks` | Set hardware timer | Currently no-op; could inform JS of next wake deadline |
| `port_get_raw_ticks` | Read hardware timer | Read CLOCK_MONOTONIC |

Note: `port_wake_main_task` and `port_task_yield` don't map to sleep/wake
on WASM. They're boundary-crossing signals:
- "wake" = "the port has new information, supervisor should look"
- "yield" = "supervisor is done for this frame, JS can proceed"

## Data Flow Example: Button Press

```
1. User clicks button in browser
2. JS hardware module detects click
3. JS submits SH_EVT_HW_CHANGE to event ring
4. Next cp_hw_step:
   a. hal_step() → sh_drain_event_ring() → sh_on_event()
   b. sh_on_event routes HW_CHANGE → updates /hal/gpio state
   c. cp_wake_check_event() — any context waiting on this pin? wake it
   d. Background callbacks drain (display refresh, etc.)
   e. sh_export_state() — write updated state for JS to read
5. Next cp_step:
   a. Supervisor picks context (maybe the one that was just woken)
   b. VM resumes Python code
   c. Python reads digitalio.DigitalInOut → common-hal → hal_gpio_fd → MEMFS
   d. Python sees button is pressed, runs handler
   e. VM yields at budget boundary
6. JS reads framebuffer, paints canvas
```

Hardware updates (steps 4a-4e) happen regardless of whether the VM runs
(step 5). A sleeping VM doesn't block hardware state from updating.

## File Organization (Current → Proposed)

Current `supervisor.c` handles both port-boundary exports AND internal
supervisor coordination. Proposed split:

```
supervisor.c    — Internal: state machine, cp_init, cp_step, cp_hw_step,
                  sh_on_event routing, context scheduling, wasm_background_tasks.
                  This is the supervisor proper — port/VH ↔ VM.

port.c          — Boundary: port_* interface (port_wake_main_task,
                  port_background_task, port_idle_until_interrupt, etc.)
                  Plus WASM exports that are port-boundary operations:
                  cp_exec, cp_ctrl_c, cp_wake, cp_cleanup, cp_print,
                  cp_banner, cp_complete, cp_input_buf_addr/size.
                  These are where JS enters/exits C.

hal.c           — Hardware endpoints: /hal/ fds, hal_step, hal_export_dirty
semihosting.c   — Bus: event ring (JS→C), state export (C→JS), trace ring (C→JS)
context.c       — Contexts: storage, scheduling, wake registrations
vm_yield.c      — VM protocol: yield, budget, suspend sentinel, line tracking
compile.c       — Compilation: source → code_state
serial.c        — I/O multiplexer: display terminal vs console
micropython.c   — MP HAL: stdin/stdout routing
```

The key move: JS-facing exports (`cp_exec`, `cp_ctrl_c`, etc.) move from
`supervisor.c` to `port.c`. They're port-boundary operations — where JS
crosses into C. `supervisor.c` becomes purely internal coordination.

## `wasm_frame` — One Entry Point

The current design has JS calling two C functions per frame (`cp_hw_step`
then `cp_step`), with JS context managers orchestrating the order. This
puts scheduling logic in JS that belongs in C.

`wasm_frame` collapses both into one call with the correct internal
ordering guaranteed by C:

```c
// One frame of the WASM process.
// Internally: drain events → update VH → schedule/run VM → export state.
// JS provides time and budget. C decides what to do.
int wasm_frame(uint32_t now_ms, uint32_t budget_ms);
```

The internal loop:

```
wasm_frame(now_ms, budget_ms):
    1. Port check  — drain event ring, update VH, run background callbacks
    2. Supervisor   — pick context, check deadlines, decide what runs
    3. VM burst     — execute bytecode within remaining budget
    4. Export       — write state, trace info, bg_pending to linear memory
    5. Return       — tell JS what happened
```

### Return value — packed three-layer result

A single return code collapses three layers of information. JS needs
to see what happened at EACH layer to make good decisions. The result
is a packed `uint32_t`:

```c
//   bits  0-7:  port result
//   bits  8-15: supervisor result
//   bits 16-23: VM result
//   bits 24-31: reserved (flags, future use)

// Port layer — what happened with hardware/events
#define WASM_PORT_QUIET         0  // no events processed, no bg work
#define WASM_PORT_EVENTS        1  // drained events from ring
#define WASM_PORT_BG_PENDING    2  // background work still pending
#define WASM_PORT_HW_CHANGED    3  // hardware state changed

// Supervisor layer — what happened with scheduling
#define WASM_SUP_IDLE           0  // no contexts to run
#define WASM_SUP_SCHEDULED      1  // picked and ran a context
#define WASM_SUP_CTX_DONE       2  // a context completed this frame
#define WASM_SUP_ALL_SLEEPING   3  // all contexts sleeping

// VM layer — what happened with bytecode execution
#define WASM_VM_NOT_RUN         0  // supervisor didn't run VM
#define WASM_VM_YIELDED         1  // budget expired, more work to do
#define WASM_VM_SLEEPING        2  // time.sleep, waiting for deadline
#define WASM_VM_COMPLETED       3  // code finished normally
#define WASM_VM_EXCEPTION       4  // unhandled exception
#define WASM_VM_SUSPENDED       5  // waiting for I/O / external event

#define WASM_FRAME_RESULT(port, sup, vm) \
    ((port) | ((sup) << 8) | ((vm) << 16))
```

JS unpacks and combines with its own knowledge (tab visibility,
user settings, display presence) to make nuanced scheduling decisions:

```js
const r = exports.wasm_frame(now, 13);
const port = r & 0xFF;
const sup  = (r >> 8) & 0xFF;
const vm   = (r >> 16) & 0xFF;

// Combine C results with JS-local state
const hidden = document.hidden;
const hasDisplay = !!this._display;

if (port === PORT_QUIET && sup === SUP_IDLE && vm === VM_NOT_RUN) {
    // Nothing happening — throttle hard, but keep cursor alive
    if (hasDisplay) scheduleSlowTimer(530);  // cursor blink rate
    else this._raf = null;                   // full stop
} else if (vm === VM_YIELDED) {
    // VM has more work — ASAP unless tab is hidden
    if (hidden) setTimeout(loop, 100);
    else requestAnimationFrame(loop);
} else if (vm === VM_SLEEPING && port !== PORT_BG_PENDING) {
    // VM sleeping, no bg work — wake at deadline
    const wake = shState.yieldArg;
    if (hasDisplay) requestAnimationFrame(loop);  // keep painting
    else setTimeout(loop, wake);
} else if (port === PORT_BG_PENDING) {
    // Background work pending (display refresh, etc.) — keep going
    requestAnimationFrame(loop);
}
```

Decision matrix (examples):

| Port | Supervisor | VM | JS context | Decision |
|---|---|---|---|---|
| QUIET | IDLE | NOT_RUN | visible, display | Slow tick (cursor blink) |
| QUIET | IDLE | NOT_RUN | hidden, no display | Full stop |
| EVENTS | SCHEDULED | YIELDED | visible | rAF immediately |
| EVENTS | SCHEDULED | YIELDED | hidden | setTimeout(100) |
| BG_PENDING | ALL_SLEEPING | SLEEPING | visible | rAF (display needs refresh) |
| QUIET | ALL_SLEEPING | SLEEPING | hidden | setTimeout(deadline) |
| HW_CHANGED | CTX_DONE | COMPLETED | any | Fire onCodeDone, schedule one more frame |
| QUIET | SCHEDULED | EXCEPTION | any | Fire onError, show traceback |

### What JS context managers become

With `wasm_frame` handling the internal ordering, JS context managers
simplify from "directors of C execution" to "consumers of C output":

- **DisplayContext**: reads framebuffer after wasm_frame, paints canvas,
  manages cursor blink timer. No longer calls cp_hw_step.
- **HardwareContext**: reads HAL state after wasm_frame, updates board SVG,
  handles onChange callbacks. No longer calls preStep/postStep around C.
- **IOContext**: polls external devices on its own timer, submits events
  to the ring for the next wasm_frame to process.
- **VMContext**: goes away — wasm_frame handles VM scheduling internally.

The JS loop becomes:

```js
function loop(now) {
    // Submit any pending events (keyboard, hardware, timers)
    flushPendingEvents();

    // One C call does everything
    const result = exports.wasm_frame(now, 13);

    // Read output, update display
    display.paint();
    hardware.sync();
    debug.update(board.traceInfo);

    // Schedule next based on what C reported
    scheduleNext(result);
}
```

### Why this is better

1. **Correct ordering guaranteed**: Port → supervisor → VM happens in C
   where the dependencies are known. JS can't accidentally call cp_step
   before cp_hw_step.

2. **Less JS↔C boundary crossing**: One WASM call per frame instead of
   two (or four, with context managers calling exports directly). Each
   boundary crossing has overhead (DataView creation, argument marshaling).

3. **C controls the budget**: C knows how much time port/supervisor work
   took and gives the VM only the remaining budget. JS doesn't have to
   guess how to split time between cp_hw_step and cp_step.

4. **Simpler JS**: The context managers become pure consumers — they read
   state and update the UI. They don't need to know about C internals
   like cp_state() transitions or background callback pending flags.

5. **Matches real hardware**: On a real board, `supervisor_run()` is one
   loop that does port → supervisor → VM → repeat. `wasm_frame` is one
   iteration of that loop, called by JS instead of running forever.
