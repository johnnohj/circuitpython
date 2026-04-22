# Port-Supreme Architecture

## Core Principle

**The port is supreme.** It owns all C memory, provides all hardware
abstractions, and is the sole boundary where JS and CircuitPython meet.
Every other layer — supervisor, VM, contexts — receives its resources
from the port and operates within port-granted allocations.

This matches the upstream CircuitPython contract (see
`supervisor/port_heap.h:12–16`): the port provides a heap for all
allocations that live outside the VM, and the VM heap is allocated into
it. Our port's twist: the WASM instance IS the port, and JS is the
physical world it interfaces with.

## Layering

```
┌──────────────────────────────────────────────────────┐
│  JS (the physical world)                             │
│  ─ DOM events, canvas, IndexedDB, WebUSB, time       │
│  ─ provides definition.json on boot                  │
│  ─ delivers UI events to HAL endpoints               │
│  ─ renders from exported display memory              │
│  ─ bootstraps the frame loop (one initial call)      │
└──────────────────┬───────────────────────────────────┘
                   │  wasm_frame() — single entry point
                   ▼
┌──────────────────────────────────────────────────────┐
│  PORT  (the WASM instance)                           │
│  ─ owns ALL linear memory                            │
│  ─ allocates regions: GC heap, pystacks, sup state   │
│  ─ C common-hal bridges JS↔Python (synchronous)      │
│  ─ HAL modules watch own endpoints, alert supervisor │
│  ─ ctx0 available as shadow context (optional)       │
│  ─ this is WHERE WE MEET JS                          │
├──────────────────────────────────────────────────────┤
│  SUPERVISOR  (mediates port ↔ VM)                    │
│  ─ context scheduler (round-robin within budget)     │
│  ─ compile service                                   │
│  ─ board/cpu/filesystem — built on port components   │
│  ─ background callbacks                              │
│  ─ does NOT know about JS                            │
├──────────────────────────────────────────────────────┤
│  VM  (runs on port-allocated memory)                 │
│  ─ GC heap: port-allocated slab                      │
│  ─ pystacks: port-allocated per-context slabs        │
│  ─ yields on budget exhaustion                       │
│  ─ never sees JS, never knows about frames           │
└──────────────────────────────────────────────────────┘
```

Note on the supervisor layer: the supervisor "owns" the board object,
CPU abstraction, filesystem, and background callbacks, but all of these
are built on top of port-provided components — MEMFS, IDBFS, HAL
endpoints, the pin table, etc. The supervisor mediates access to
port-provided resources; it doesn't create them.

## The Synchronous Bridge: C Common-HAL

The primary bridge between JS and Python is the C common-hal layer,
not a Python intermediary. WASM imports are synchronous function calls.
When Python reads a sensor value, the call chain is:

```
Python: value = analogio.AnalogIn(board.A0).value
  → C: common_hal_analogio_analogin_get_value()
    → reads HAL endpoint (MEMFS)
      — OR —
    → WASM import: js_read_adc(pin_id)
      → JS: return currentSensorValue;
    ← C receives value
  ← Python receives value
```

From Python's perspective, it called a C function and the C function
returned. The entire chain is synchronous — no SUSPEND needed, no
context switching, no scheduling overhead.

This works because WASM imports are just function calls. JS runs
synchronously in the same call stack, returns a value, and C
continues. The jsffi PVN marshaling already demonstrates this — 16
import functions, all synchronous.

### Two data paths

```
Path A — MEMFS (state already written):
  JS writes HAL endpoint (slider moved, button clicked)
  C common-hal reads MEMFS on next access
  Best for: values that change asynchronously (sensor sliders, GPIO)

Path B — Synchronous WASM import (ask JS directly):
  C common-hal calls __attribute__((import_name("js_fn")))
  JS function runs synchronously, returns value
  C returns to Python
  Best for: computed values (CPU temperature, time, random)
```

Both paths are invisible to Python. User code calls `sensor.value`
and gets a number. Whether that number came from MEMFS or a JS
function call is a common-hal implementation detail.

### When SUSPEND is needed

The synchronous bridge breaks down only when JS needs to do something
*async* — wait for user input, fetch from network, read from
IndexedDB. In those cases:

```
Python → shared-bindings → common-hal → SUSPEND → return to JS
JS does async work (rAF, fetch, user input)
Resume → common-hal returns → Python continues
```

This is the existing SUSPEND mechanism. It's needed for `time.sleep`,
`input()`, and future network I/O. It is NOT needed for sensor reads,
pin state, or any data that's already available.

## Self-Aware HAL Modules

Because C common-hal modules own their HAL endpoints, they can watch
for changes and alert the supervisor without any Python intermediary:

```c
// common-hal/digitalio/DigitalInOut.c
bool common_hal_digitalio_digitalinout_get_value(digitalio_digitalinout_obj_t *self) {
    // Read current value from HAL endpoint
    uint16_t val = hal_read_gpio(self->pin->number);

    // Detect edge — module tracks its own state
    if (val != self->last_value) {
        self->last_value = val;
        // Alert supervisor: schedule background callback for
        // interrupt-driven modules (keypad, countio, etc.)
        port_wake_main_task();
    }
    return val;
}
```

This gives us the interrupt model without a Python ISR layer:
1. JS writes HAL endpoint (button clicked)
2. C common-hal detects change on next read or background tick
3. Common-hal updates module state (keypad event queue, counter, etc.)
4. Supervisor schedules background callback if needed
5. User code reads the updated state — portable, synchronous

Interrupt-driven modules become natural:
- **keypad.Events**: common-hal detects GPIO edges, enqueues events
- **countio.Counter**: common-hal increments count on detected edge
- **rotaryio.IncrementalEncoder**: common-hal tracks quadrature state
- **touchio.TouchIn**: common-hal samples analog values

The C layer can also run periodic updates via `port_background_tick`:
pattern generators, sensor simulation, watchdog timers — all in C
background callbacks without consuming Python context resources.

## ctx0: Shadow Context (Optional)

ctx0 is available as the port's shadow Python context — a place for
Python-level port logic when C alone isn't sufficient. It is NOT a
required component of the architecture. The C common-hal + background
callbacks handle the primary data path.

### When ctx0 earns its keep

We bring a Python interpreter and runtime with us. Variability is
the name of the game. ctx0 becomes valuable when:

1. **Board-specific behaviors too variable for C**: a `_board.py`
   ships with each definition.json, configuring custom sensor
   simulation, LED patterns, or hardware quirks in Python.

2. **Gap-filling at runtime**: "assert-like" functionality where user
   code exercises a path the C layer didn't anticipate. CPU
   temperature, for example — ctx0 can intercept and provide a
   sensible value on-the-fly.

3. **Rapid prototyping**: iterate on port-level logic in Python
   before committing to C. Move proven patterns to common-hal later.

4. **Complex multi-device scenarios**: coordinating multiple external
   hardware targets (WebUSB, WebSerial) where the orchestration logic
   is too complex for C background callbacks.

5. **Community extensibility**: users write board-specific behavior
   in Python without recompiling the WASM binary.

### What ctx0 looks like when used

```python
# frozen/_board.py (loaded into ctx0 on boot, if present)
import jsffi

# Fill in CPU temperature — not a real sensor, but code.py may ask
@jsffi.on_hal_read('/hal/cpu/temperature')
def cpu_temp():
    return 25.0 + jsffi.random() * 5.0  # simulated

# Custom board behavior: CPX-style capacitive touch calibration
@jsffi.on_hal_init('/hal/touch/*')
def calibrate_touch(endpoint):
    # Run a calibration sequence specific to this board's touch layout
    baseline = jsffi.read_analog(endpoint.pin)
    endpoint.threshold = baseline + 100
```

### When to use C vs Python

```
C common-hal (primary path):
  ✓ Sensor reads — synchronous WASM import or MEMFS read
  ✓ Pin state management — GPIO, ADC, PWM
  ✓ Bus operations — I2C, SPI, UART
  ✓ Pattern generators — simple math in background callback
  ✓ Edge detection — module tracks own state
  ✓ Background periodic tasks — port_background_tick

ctx0 Python (when needed):
  ✓ Board-specific customization — ships with definition.json
  ✓ Complex orchestration — multi-device coordination
  ✓ Gap-filling — runtime assertions for unanticipated paths
  ✓ Prototyping — iterate fast, move to C when stable
  ✓ Community extensions — no recompilation needed
```

The deciding question: **does this need to vary per board definition,
or is it universal port behavior?** Universal → C. Per-board → Python
(ctx0 with `_board.py`).

## The Frame Loop

### C-driven (primary model)

The port drives the frame loop from C, requesting animation frames
via WASM import:

```c
// port.c — frame loop
__attribute__((import_name("js_request_animation_frame")))
extern void js_request_animation_frame(void);

int wasm_frame(uint32_t now_ms, uint32_t budget_ms) {
    // 1. Port: drain events, update HAL, run background callbacks
    port_step(now_ms);

    // 2. Supervisor: pick context, run VM within remaining budget
    int result = supervisor_step(now_ms, budget_ms);

    // 3. Request next frame if there's work to do
    if (has_active_work(result)) {
        js_request_animation_frame();  // synchronous WASM import
    }
    // else: idle — no rAF, zero CPU (WFE equivalent)

    return result;
}
```

JS bootstraps with one call. After that, C decides whether to keep
the loop going:

```js
// Bootstrap
wasm.exports.wasm_frame(performance.now(), 13);

// The js_request_animation_frame import:
env.js_request_animation_frame = () => {
    requestAnimationFrame((now) => {
        wasm.exports.wasm_frame(now, 13);
    });
};
```

### ctx0-driven (when Python port logic is active)

If ctx0 is running `_board.py`, it can take over frame loop ownership
from C. ctx0 calls `jsffi.request_animation_frame()`, which triggers
SUSPEND, returns to JS, and resumes on the next frame:

```python
# frozen/_board.py (ctx0) — only when board needs Python-level logic
import jsffi

while True:
    # Board-specific per-frame work
    update_touch_calibration()
    run_custom_led_pattern()

    # Request next frame — SUSPEND back to JS
    jsffi.request_animation_frame()
    # Resumes here on next frame
```

The supervisor handles the transition: if ctx0 exists and is running,
it gets first slice of the frame budget. If ctx0 is idle or absent,
C drives the loop directly.

### Idle / deep sleep

Whether C-driven or ctx0-driven, idle behavior is the same: don't
request the next frame. Zero CPU usage. This is the WASM equivalent
of WFE (Wait For Event) on ARM.

Wake-up: JS writes to a HAL endpoint (user interaction), which calls
a WASM export that triggers the next `wasm_frame()`.

## Memory Ownership

The port explicitly owns and exports its memory regions. No custom
allocator is needed — upstream CircuitPython uses linker-driven static
placement for DTCM/ITCM, not runtime allocation. We follow the same
model: static arrays at known addresses, port hands pointers to
consumers.

```
WASM Linear Memory (4 MB initial, growable)
┌──────────────────────────────────────────┐
│  Port-owned statics                      │
│  ┌────────────────────────────────────┐  │
│  │ GC heap               512 KB      │  │  ← gc_init(heap, heap+size)
│  ├────────────────────────────────────┤  │
│  │ PyStacks (8 × 8 KB)    64 KB      │  │  ← per-context, pointer-swapped
│  ├────────────────────────────────────┤  │
│  │ Supervisor state        4 KB      │  │  ← input buffer, etc.
│  ├────────────────────────────────────┤  │
│  │ Pin table, HAL state              │  │  ← populated from definition.json
│  ├────────────────────────────────────┤  │
│  │ Display framebuffer               │  │  ← exported to JS for canvas
│  └────────────────────────────────────┘  │
├──────────────────────────────────────────┤
│  C heap (libc malloc)                    │  ← port_malloc delegates here
│  Dynamic port-level allocations          │
├──────────────────────────────────────────┤
│  C stack                    512 KB       │  ← linker-reserved
│  (VM limited to 16 KB via cstack_init)   │
└──────────────────────────────────────────┘
```

PLACE_IN_DTCM / PLACE_IN_ITCM macros remain identity — correct for
WASM's flat address space. The organization above is the semantic
equivalent: port-owned statics at known addresses, accessible via
exported pointers.

## Scheduling Within a Frame

The supervisor divides each frame's budget between contexts. The
primary model has no Python port context — just C port work followed
by user code:

```
wasm_frame(now=16.7ms, budget=13ms):
  ┌─ C port step                                ~1 ms
  │   - drain JS events from HAL endpoints
  │   - run background callbacks (display, ticks)
  │   - HAL modules detect changes, update state
  ├─ ctx0 (if active, optional)                  ~1-2 ms
  │   - board-specific Python logic
  │   - gap-filling, custom behaviors
  ├─ ctx1 runs with remaining budget             ~10-11 ms
  │   - user code executes
  │   - reads pin values (already current)
  │   - writes display (common-hal → framebuffer)
  ├─ background callbacks drain                   ~1 ms
  │   - display refresh
  │   - tick catchup
  └─ C decides: request next frame or idle
```

ctx1 never needs to suspend for I/O. By the time it runs, C (and
optionally ctx0) has already updated all HAL state. Pin reads return
current values immediately.

## The Interrupt Model

On real hardware:
1. Pin changes state → ISR fires → sets flag
2. Background callback picks it up
3. User code reads new value on next `digitalio` read

Our equivalent:
1. User clicks button in browser → JS event fires
2. JS writes to HAL endpoint (`/hal/gpio/BUTTON_A`)
3. C port step: common-hal detects change, updates module state
4. Background callback fires if needed (keypad event, counter increment)
5. Supervisor gives ctx1 its budget slice
6. ctx1 reads `digitalio.DigitalInOut.value` → common-hal → current value

The C common-hal layer IS the ISR equivalent. Each module watches its
own endpoints, detects edges, and maintains state. No Python
intermediary needed for the core interrupt model.

For board-specific interrupt behaviors (custom debounce, multi-pin
gestures, calibration sequences), ctx0's `_board.py` can augment the
C layer.

## Board Definition at Boot

JS parses `definition.json` and provides the results to the port at
boot. The port doesn't know or care where the definition came from —
GitHub repo, local filesystem, IndexedDB, hardcoded default.

```
Boot sequence:
  1. JS fetches/loads definition.json
  2. JS instantiates WASM
  3. JS writes parsed pin table to port memory (MEMFS or exported region)
  4. JS calls cp_init() — port reads pin table, sets up common-hal
  5. JS calls wasm_frame() — C port loop starts (or ctx0 if _board.py present)
```

This supports future board switching: JS loads a different
definition.json (Feather pinout, CPX pinout, custom), writes it to the
port, triggers a full reset. The port rebuilds its pin table and
common-hal state from the new definition. The SVG path comes from the
definition; JS fetches and renders it.

Optionally, a `_board.py` ships alongside the definition.json. If
present, it's loaded into ctx0 at boot and provides board-specific
Python-level behaviors. If absent, the port runs purely in C.

## Cross-Context State

User code in ctx1 accesses port-provided state through the normal
CircuitPython module API:

```python
# code.py — runs in ctx1, fully portable
import board
import analogio
import digitalio

sensor = analogio.AnalogIn(board.A0)    # common-hal → HAL endpoint
button = digitalio.DigitalInOut(board.BUTTON_A)
print(sensor.value)                      # reads current value (synchronous)
```

No cross-context imports. No jsffi. No port-specific API. This code
runs identically on a physical Feather.

### State that survives code.py restarts

- **Pin state**: lives in HAL endpoints (MEMFS). Survives ctx1 teardown.
- **Display state**: framebuffer is port memory. Display persists across runs.
- **Filesystem**: IDBFS-backed CIRCUITPY. Persistent across everything.
- **Board behaviors**: ctx0 state (if active). Custom patterns continue.

### State that resets with code.py

- **Claimed pins**: ctx1's pin claims are released on ctx1 teardown.
- **Open buses**: I2C/SPI/UART handles closed.
- **Python globals**: ctx1's namespace is destroyed.

This matches real hardware behavior: unplug your code.py (Ctrl-C),
the hardware returns to unclaimed state, but the physical
sensors/buttons are still there.

## What Changes from Today

| Aspect | Current | Port-Supreme |
|--------|---------|--------------|
| JS↔Python bridge | Event ring + MEMFS | C common-hal synchronous imports (primary) + MEMFS |
| Frame loop owner | JS (`requestAnimationFrame`) | C port decides (or ctx0 if active) |
| Sensor reads | JS writes MEMFS, C reads | Same, or C calls JS import synchronously |
| Interrupt model | None (polling only) | C common-hal detects edges, background callbacks |
| Event delivery | Semihosting event ring | JS writes HAL endpoint, C detects on next step |
| Idle behavior | JS decides whether to call `wasm_frame` | C decides whether to request next frame |
| Board definition | Baked into C at compile time | JS provides at boot, port can switch boards |
| Port Python logic | None | Optional ctx0 with `_board.py` when needed |
| Context model | ctx0 = code.py, others auxiliary | ctx0 = optional port shadow, ctx1+ = user code |

## Implementation Path

### Phase 1: Port memory ownership
- Group port statics into explicit struct or region
- Port exports base/size for JS inspection
- No functional change — just makes ownership explicit

### Phase 2: Self-aware HAL modules
- Common-hal modules watch own endpoints for changes
- Edge detection, dirty flags, background callback scheduling
- Pattern generators in C background callbacks
- Interrupt-driven modules (keypad, countio, rotaryio) via C

### Phase 3: C-driven frame loop
- Port requests rAF via synchronous WASM import
- C decides idle vs active (WFE equivalent)
- JS bootstrap simplifies to one initial `wasm_frame()` call

### Phase 4: Synchronous JS callout
- Common-hal modules call WASM imports for computed values
- CPU temperature, entropy, time — JS provides synchronously
- No SUSPEND needed for data reads

### Phase 5: Dynamic board definition
- JS parses definition.json, writes to port at boot
- Port rebuilds pin table from definition
- Board switching via JS providing new definition + reset
- Optional `_board.py` loaded into ctx0 for board-specific Python logic

## Design Constraints

1. **code.py portability**: user code must run unmodified on real hardware.
   No jsffi imports, no port-specific APIs in user-facing code.

2. **C is the primary bridge**: common-hal modules handle the JS↔Python
   data path synchronously. Python intermediaries are optional, not
   required.

3. **No VM awareness of JS**: the VM never knows about frames, DOM events,
   or browser APIs. It runs Python and yields when told to.

4. **Supervisor mediates, doesn't originate**: the supervisor coordinates
   port-provided resources. It doesn't create hardware abstractions or
   know about JS.

5. **MEMFS is the substrate**: HAL endpoints, filesystem state, and
   cross-layer communication all flow through MEMFS. No bypassing
   with C statics or direct memory manipulation.

6. **Single entry point**: `wasm_frame()` is the only JS→C call during
   normal operation. Bootstrap calls (`cp_init`) and edge cases
   (`cp_ctrl_c`) are the exceptions.

7. **ctx0 is optional**: the architecture works without a Python port
   context. ctx0 earns its place only when Python-level variability
   is genuinely needed (per-board customization, gap-filling, complex
   orchestration).
