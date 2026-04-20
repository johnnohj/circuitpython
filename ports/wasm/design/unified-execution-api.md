# Unified Execution API — Proposal

## Principle

It doesn't matter where the Python comes from. One execution primitive,
one cleanup primitive, one state query. JS owns UX. C owns the VM.
VM and VH (Virtual Hardware) are separate machines sharing MEMFS.

---

## C Exports

### Initialization — Two Machines

```c
// Layer 2a: Python VM — mp_init, GC, pystack, compile service
void cp_init(void);

// Layer 2b: Virtual Hardware — HAL fds, display framebuffer,
// supervisor terminal, initial pin state.
// Called once after cp_init. Sets up the /hal/ MEMFS endpoints
// and the display surface that cp_hw_step refreshes.
void cp_hw_init(void);
```

### Per-Frame Stepping — Independent Machines

```c
// VM work: bytecode stepping within frame budget (~13ms).
// Only meaningful when cp_state() == EXECUTING.
// No-op when READY or SUSPENDED.
void cp_step(uint64_t now_ms);

// VH work: display refresh, background callbacks, terminal cursor.
// Must be called EVERY frame regardless of VM state.
// In READY: refreshes supervisor terminal.
// In EXECUTING: refreshes user's displayio output.
// In SUSPENDED: keeps display alive while VM sleeps.
void cp_hw_step(uint64_t now_ms);
```

JS calls both each frame normally:

```js
_loop(now) {
    this._exports.cp_step(now);      // VM (no-op if not executing)
    this._exports.cp_hw_step(now);   // VH (always)
    this._display?.paint();
    // ...
}
```

When VM is suspended, JS can skip cp_step or call it anyway (it's a no-op).
cp_hw_step always runs.

### Execution

```c
// Execute Python from a string or file path.
// Returns 0 on success (execution started), non-zero on compile error.
//
// kind:
//   0 = CP_EXEC_STRING  — compile+run the string in input_buf
//   1 = CP_EXEC_FILE    — compile+run the file path in input_buf
//
// After this returns 0, cp_step() drives execution frame-by-frame.
// Query cp_state() to know when it finishes.
int cp_exec(int kind, int len);

// Current state:
//   0 = READY       — not executing, hardware at initial state
//   1 = EXECUTING   — vm_yield_step running
//   2 = SUSPENDED   — paused (sleep, WFE, await)
int cp_state(void);

// Resume from SUSPENDED state. Called by JS when a wake event
// (Promise resolution, timer, I/O completion) occurs.
// No-op if not SUSPENDED.
void cp_wake(void);

// Stop execution + reset Layer 3 (pins, buses, display group, GC).
// Idempotent. After this, cp_state() == READY.
void cp_cleanup(void);
```

### Helpers

```c
int  cp_input_buf_addr(void);       // shared string buffer
int  cp_input_buf_size(void);
void cp_ctrl_c(void);               // schedule KeyboardInterrupt
void cp_banner(void);               // print version banner
int  cp_complete(int len);          // tab-completion
int  cp_syntax_check(int len);      // multiline detection
```

### What's Gone

```
cp_run(src_kind, len, ctx, priority)  → cp_exec(kind, len)
cp_soft_reboot()                      → cp_cleanup()
cp_ctrl_d()                           → cp_cleanup()
cp_reset_display()                    → inside cp_cleanup()
cp_is_runnable()                      → cp_state()
cp_get_yield_reason()                 → cp_state() == SUSPENDED
```

---

## JS API

### CircuitPython class

```js
class CircuitPython {
    // ── Lifecycle ──
    static async create(options)    // Layer 1 + cp_init + cp_hw_init
    destroy()                       // tear down everything

    // ── Execution (source-agnostic) ──
    exec(code)                      // execute a Python string
    execFile(path)                  // execute a .py file from MEMFS
    stop()                          // interrupt + cleanup → READY

    // ── State ──
    get state()                     // 'ready' | 'executing' | 'suspended'

    // ── Events ──
    onStdout(text)                  // output callback
    onStderr(text)                  // error/debug callback
    onDone()                        // execution finished → ready

    // ── Filesystem ──
    readFile(path)                  // read from MEMFS
    writeFile(path, data)           // write to MEMFS (auto-IDB)

    // ── Hardware ──
    hardware(name)                  // get hardware module by name
}
```

### What's Gone from JS

```
runBoardLifecycle()           — JS calls exec/execFile directly
_enterRepl()                  — JS readline loop + exec()
_waitingForKey                — JS decides after onDone
_codeDoneFired                — onDone fires once per exec
_scheduleAutoReload()         — JS writeFile + stop + execFile
ctrlC() / ctrlD()             — stop()
```

---

## REPL — Just exec() In A Loop

```js
function startRepl(board, display) {
    const readline = new Readline(display);

    readline.onLine = (line) => {
        board.exec(line);
    };

    board.onDone = () => {
        readline.showPrompt();
    };

    readline.showPrompt();
}
```

Stopping the REPL: `board.stop()`. The readline stops receiving
onDone callbacks, no more prompts appear. Done.

## Running code.py — Just execFile()

```js
function runCode(board, editor) {
    board.writeFile('/CIRCUITPY/code.py', editor.value);
    board.execFile('/code.py');
    board.onDone = () => {
        status.textContent = 'Done';
    };
}
```

---

## State Machine

```
              create()
                │
           cp_init + cp_hw_init
                │
                ▼
    ┌─────── READY ◄──────────────┐
    │           │                  │
    │    exec() / execFile()       │
    │           │                  │
    │           ▼                  │
    │      EXECUTING ──────────────┤ onDone (finished)
    │        │    ▲                │
    │  sleep/│    │ cp_wake()      │
    │   WFE  │    │ (Promise       │
    │        ▼    │  resolved)     │
    │      SUSPENDED               │
    │           │                  │
    │       stop() from any state  │
    │           │                  │
    │      [cp_cleanup]            │
    │           │                  │
    └───────────┘──────────────────┘
```

---

## SUSPEND → Promise → Wake

The suspend reason tells JS what to wait for. The JS Promise IS
the event. cp_wake() means "the promise resolved, resume."

### time.sleep()

```
Python: time.sleep(1)
  → VM hits sleep → returns SUSPEND
  → cp_state() == SUSPENDED
  → JS reads sleep duration from export/MEMFS
  → JS: setTimeout(duration, () => board._exports.cp_wake())
  → Next cp_step() resumes VM from suspend point
```

### I/O Wait (future: WebUSB sensor read)

```
Python: sensor.temperature
  → common-hal does I2C read → MEMFS endpoint empty → SUSPEND
  → cp_state() == SUSPENDED, reason = I2C_READ
  → JS: i2cDevice.transferIn(register)        // WebUSB Promise
        .then(data => {
            memfs.write('/hal/i2c/dev/72', data);
            board._exports.cp_wake();
        })
  → Next cp_step() resumes, common-hal reads /hal/i2c/dev/72
```

### asyncio await (future)

```
Python: await asyncio.sleep(0.1)
  → Generator yields → VM returns SUSPEND
  → JS: setTimeout(100, () => cp_wake())
  → VM resumes generator from yield point
```

### Key Insight from MicroPython

MicroPython returns the Promise object itself through the proxy layer.
We don't need that complexity — our MEMFS layer is the intermediary.
The suspend reason is an integer enum. JS maps it to whatever Promise
source is appropriate (timer, WebUSB, WebSerial, fetch, etc.).

But the principle is the same: **a JS Promise resolving IS the event
that WFE waits for.**

```
mp_resume(gen, result)    ≈    cp_wake() after writing result to MEMFS
MP_VM_RETURN_YIELD        ≈    cp_state() == SUSPENDED
```

---

## Hardware Module Reset

Each JS hardware module gets a `reset()` method that restores its
MEMFS endpoints to initial state:

```js
class GpioModule {
    reset(memfs) {
        // All 64 pins: unclaimed, input, value=0, no pull
        memfs.writeFile('/hal/gpio', new Uint8Array(64 * 8));
    }
}

class NeoPixelModule {
    reset(memfs) {
        const data = memfs.readFile('/hal/neopixel');
        if (data) data.fill(0);  // all LEDs off
    }
}
```

`stop()` calls cp_cleanup() (C side) then resets hardware modules (JS side):

```js
stop() {
    this._exports.cp_ctrl_c();
    this._exports.cp_cleanup();    // Layer 3 C state
    for (const mod of this._hw.modules()) {
        mod.reset(this._wasi);     // Layer 3 MEMFS state
    }
}
```

---

## Frame Loop

```js
_loop(now) {
    // Always: VH step (display, hardware sync)
    this._exports.cp_hw_step(now);
    this._hw.preStep(this._wasi);
    this._hw.postStep(this._wasi);

    // VM step (no-op if READY or SUSPENDED)
    this._exports.cp_step(now);

    // Display
    this._display?.paint();
    if (this.state === 'ready' && this._replActive) {
        this._display?.drawCursor();
    }

    // Transition detection
    const st = this.state;
    if (this._prevState === 'executing' && st === 'ready') {
        this._onDone?.();
    }
    this._prevState = st;

    requestAnimationFrame((t) => this._loop(t));
}
```

---

---

## Line-Level Execution Control

### Motivation

JS should be able to treat the Python VM as a kernel — observing and
controlling execution at source-line granularity. This enables step
debugging, breakpoints, hot-reload, coverage, and the "JS as scheduler"
model where JS decides per-line whether to proceed, pause, or inject.

### Existing Infrastructure

Three mechanisms in the VM already provide the pieces:

**1. MICROPY_VM_HOOK_LOOP** (py/vm.c:1387)
Fires at every backwards branch (loop iteration, function return).
Currently runs `RUN_BACKGROUND_TASKS`. This is the natural point for
line-level hooks — it's frequent enough for control but not every-opcode
expensive.

**2. MICROPY_PY_SYS_SETTRACE** (py/profile.c)
Full CPython-compatible tracing. `TRACE_TICK` fires at every instruction
and detects LINE events by comparing prev_line_no != current_line_no:

```c
// py/profile.c:278-287
size_t current_line_no = mp_prof_bytecode_lineno(rc, ip - prelude->opcodes);
if (prev_line_no != current_line_no) {
    args->frame->lineno = current_line_no;
    args->event = MP_OBJ_NEW_QSTR(MP_QSTR_line);
    top = mp_prof_callback_invoke(callback, args);
}
```

Events: CALL, LINE, RETURN, EXCEPTION, (OPCODE — stubbed).
`mp_prof_bytecode_lineno()` resolves bytecode offset → source line
via the line number table embedded in compiled bytecode.

**3. MICROPY_VM_YIELD_ENABLED** + MP_VM_RETURN_SUSPEND (py/vm.c:1400-1414)
Our cooperative suspension mechanism. At VM_HOOK_LOOP, if
`mp_vm_should_yield()` returns true, the VM saves ip/sp/exc_sp to
code_state and returns SUSPEND. C stack unwinds cleanly. The driver
resumes later by re-entering `mp_vm_step()`.

### Combining Them: Line-Level Suspend

The key insight: SETTRACE already knows when execution crosses a
source line boundary. SUSPEND already knows how to pause cleanly.
Combine them at VM_HOOK_LOOP:

```c
// New hook at backwards branches (VM_HOOK_LOOP or adjacent):
#if MICROPY_WASM_LINE_HOOKS
{
    const mp_raw_code_t *rc = code_state->fun_bc->rc;
    size_t current_line = mp_prof_bytecode_lineno(
        rc, ip - rc->prelude.opcodes);
    if (current_line != _wasm_prev_line) {
        _wasm_prev_line = current_line;
        _wasm_current_line = current_line;  // exported for JS to read
        if (_wasm_line_step_mode) {
            // Save state and yield to JS at every line transition
            code_state->ip = ip;
            code_state->sp = sp;
            nlr_pop();
            FRAME_LEAVE();
            return MP_VM_RETURN_SUSPEND;
        }
    }
}
#endif
```

### C Exports for Line Control

```c
// Current source line of executing code (0 if not executing).
// Updated at every line transition when line hooks are enabled.
int cp_current_line(void);

// Enable/disable line-step mode.
// When enabled, VM suspends at every source line transition.
// cp_state() returns SUSPENDED, cp_current_line() gives the line.
// JS calls cp_wake() to advance to the next line.
void cp_set_line_step(int enabled);

// Set a breakpoint at a source line. VM suspends when it reaches
// this line. Multiple breakpoints supported.
void cp_set_breakpoint(int line);
void cp_clear_breakpoint(int line);
void cp_clear_all_breakpoints(void);
```

### JS Usage

**Step debugging:**
```js
board._exports.cp_set_line_step(1);
board.exec(code);

board.onSuspend = () => {
    const line = board._exports.cp_current_line();
    editor.highlightLine(line);
    // User clicks "Step" → cp_wake()
    // User clicks "Continue" → cp_set_line_step(0) + cp_wake()
};
```

**Breakpoints:**
```js
board._exports.cp_set_breakpoint(7);
board.exec(code);

board.onSuspend = () => {
    const line = board._exports.cp_current_line();
    if (line === 7) {
        // Hit breakpoint — inspect state, show variables
        inspector.show(board.getLocals());
    }
};
```

**Coverage / profiling:**
```js
// Don't suspend, just observe line transitions via a MEMFS log
// or a lightweight export that JS polls each frame
const coverage = new Set();
board.onFrame = () => {
    coverage.add(board._exports.cp_current_line());
};
```

**JS-controlled iteration limit:**
```js
let iterations = 0;
board._exports.cp_set_line_step(1);
board.exec('while True:\n    process()');

board.onSuspend = () => {
    iterations++;
    if (iterations > 1000) {
        board.stop();  // JS decides "enough"
    } else {
        board._exports.cp_wake();
    }
};
```

### Performance Considerations

**Cost of mp_prof_bytecode_lineno():** Binary search through the line
number table. On real microcontrollers (MHz, flash latency) this is
measurable. On WASM (GHz, linear memory reads at native speed) it's
negligible. A `while True: pass` loop hits VM_HOOK_LOOP every iteration;
adding a line lookup there costs ~50ns per iteration on modern hardware.
At 60fps with 13ms budget, that's ~260,000 iterations per frame with
line tracking vs ~300,000 without. Acceptable.

**When disabled:** The `#if MICROPY_WASM_LINE_HOOKS` guard compiles
out entirely in production builds. Zero overhead.

**Selective enabling:** Even when compiled in, `_wasm_line_step_mode`
is a runtime flag. The check `if (current_line != _wasm_prev_line)`
short-circuits when the line hasn't changed (tight inner loops).
The suspend path only triggers when `_wasm_line_step_mode` is true.

---

## Debug Build Support

### Build Variants

The WASM port can produce debug builds alongside production:

```makefile
# Production: optimized, no line hooks, no settrace
make VARIANT=browser

# Debug: line hooks, settrace, verbose output, assertions
make VARIANT=browser-debug
```

The debug build enables:

```c
// mpconfigvariant-debug.h
#define MICROPY_WASM_LINE_HOOKS     (1)
#define MICROPY_PY_SYS_SETTRACE     (1)
#define MICROPY_DEBUG_VERBOSE       (1)
#define NDEBUG                      // keep asserts
#define WASM_DEBUG_BUILD            (1)
```

### Why Debug Builds Are Cheap in WASM

On real hardware, debug overhead matters:
- ARM Cortex-M4 at 120 MHz, flash read latency 2-5 cycles
- SETTRACE per-instruction callback: ~30% throughput reduction
- Extra RAM for trace state: pressure on 256K SRAM

In WASM, none of this applies:
- Host CPU at 3-5 GHz, linear memory reads at L1 cache speed
- SETTRACE overhead < 5% of frame budget
- Memory is plentiful (browser tab gets hundreds of MB)
- WASM binary size increase ~50K for debug symbols — irrelevant for dev

A debug build running at 50% of production speed is still faster than
real CircuitPython on an RP2040. The user won't notice.

### Debug Output Channels

Debug builds can expose additional information via exports and MEMFS:

```c
// Exports (debug build only):
int  cp_debug_gc_used(void);         // bytes of GC heap in use
int  cp_debug_gc_free(void);         // bytes of GC heap free
int  cp_debug_pystack_used(void);    // pystack bytes in use
int  cp_debug_frame_depth(void);     // current call stack depth
int  cp_debug_bytecodes_executed(void); // total bytecodes since last reset
void cp_debug_dump_state(void);      // print full VM state to stderr
```

```
// MEMFS endpoints (debug build only):
/hal/debug/gc         — GC heap stats (updated each cp_hw_step)
/hal/debug/vm         — VM state snapshot (ip, sp, line, frame depth)
/hal/debug/trace      — ring buffer of recent LINE events
/hal/debug/perf       — per-frame timing (vm_step_us, hw_step_us, idle_us)
```

JS reads these endpoints in the normal postStep cycle:

```js
if (board.isDebugBuild) {
    const vm = board._wasi.readFile('/hal/debug/vm');
    debugPanel.update({
        line: readU32(vm, 0),
        frameDepth: readU32(vm, 4),
        gcUsed: readU32(vm, 8),
        bytecodes: readU32(vm, 12),
    });
}
```

### SETTRACE Integration

In debug builds, `sys.settrace` is available from Python:

```python
import sys

def tracer(frame, event, arg):
    if event == 'line':
        print(f"  line {frame.f_lineno}")
    return tracer

sys.settrace(tracer)
```

But for WASM, the more useful path is C-level tracing exported to JS
(via the line hooks above), since Python-level settrace has significant
overhead from the callback dispatch through the interpreter.

The C-level hooks give JS the same information (line numbers, call/return
events) without re-entering the Python VM for each trace event.

### Debug Build + Line Hooks + SUSPEND Interaction

All three mechanisms compose cleanly:

```
Backwards branch (VM_HOOK_LOOP)
  │
  ├─ RUN_BACKGROUND_TASKS          (always)
  │
  ├─ Line hook: update current_line (debug build)
  │   └─ if line_step_mode → SUSPEND
  │   └─ if breakpoint[line] → SUSPEND
  │
  ├─ Budget check: mp_vm_should_yield()
  │   └─ if budget exhausted → SUSPEND (frame budget)
  │
  └─ Pending exception check
      └─ if ctrl_c → KeyboardInterrupt
```

The first SUSPEND wins — if a breakpoint fires on the same iteration
as a budget exhaustion, the breakpoint takes priority (it comes first
in the sequence). JS sees SUSPENDED and reads the reason to know which
triggered it.

---

## Migration Path

1. **C: add cp_state() export** — trivial, read _state enum
2. **C: add cp_hw_init() + cp_hw_step()** — factor out of cp_init/cp_step
3. **C: rename cp_run → cp_exec** with simplified signature
4. **C: add cp_wake()** — set resume flag, used by JS after Promise resolves
5. **JS: rewrite _loop()** — use cp_state() for transition detection
6. **JS: add hardware module reset()** — per-module MEMFS cleanup
7. **JS: replace runBoardLifecycle** with exec/execFile + onDone
8. **JS: REPL as readline + exec loop** — no special mode
9. **JS: index.html** — simplified three-button UI using new API
10. **Test in browser** — not just Node
