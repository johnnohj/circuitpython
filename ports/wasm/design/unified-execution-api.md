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
