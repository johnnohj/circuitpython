# The WASM Port's Interrupt Model

## Status: Unified design direction (2026-04-24)

Synthesizes insights from today's work on background tasks, event-routed
hardware, lightweight/heavyweight tick split, and pin listener registration.
Connects to: port-supreme.md, background-tasks.md, hal-gpio-endpoints.md,
yield-suspend-separation.md.

---

## The core problem

On real hardware, peripherals run continuously.  The display DMA refreshes,
NeoPixel shift registers hold state, GPIO interrupts fire instantly, ADCs
sample, timers count.  The CPU just reads results.

On WASM, **nothing runs unless we make it run.**  JS runs between frames.
C runs during frames.  During VM execution (~13ms), only code we explicitly
call from VM hooks executes.  There is no timer ISR, no DMA, no GPIO
interrupt controller.

The port needs an interrupt model that makes the simulated hardware feel
alive despite this fundamental constraint.

---

## What we have (as of today's commits)

### Three layers of periodic work

```
HOOK_LOOP (every backwards branch, ~microseconds):
  serial_check_interrupt()       Ctrl-C
  supervisor_tick()              LIGHTWEIGHT: schedule callbacks (~1ms gate)
  budget check                   yield if frame budget exhausted

HOOK_RETURN (every function return):
  background_callback_run_all()  drain queue: displayio, module work

Frame bookends (cp_hw_step, once per rAF ~16ms):
  sh_drain_event_ring()          process JS->C events
  RUN_BACKGROUND_TASKS           drain callbacks
  hal_export_dirty()             export state for JS
```

### Event ring (JS->C)

JS pushes typed events into linear memory.  C drains them at the start of
each frame.  Events carry `(type, pin, value)` — enough for C to write
MEMFS, set flags, latch values, and wake sleeping contexts.

### Pin listener registration (PoC, just landed)

When Python claims a pin, C calls a WASM import.  JS attaches DOM event
listeners (mousedown/mouseup, touchstart/touchend) to the board SVG
element for that pin.  The listener pushes `SH_EVT_HW_CHANGE` into the
event ring.  The DOM event system IS the interrupt controller.

### Lightweight/heavyweight tick split

`supervisor_tick()` (lightweight) runs at HOOK_LOOP, checks flags, and
**schedules** `supervisor_background_tick()` (heavyweight) as a callback.
The callback drains at frame bookends or function returns — never in the
bytecode hot path.

---

## What's missing

### (a) Mid-frame event visibility

JS DOM event handlers can't fire while WASM is executing (single-threaded
— the JS event loop is blocked).  Events from button clicks queue in the
browser until JS regains control between frames.

But **WASM can call JS** at any time via WASM imports.  We already do this
for `clock_gettime`, `port_request_frame`, and the pin listener registration.
From HOOK_LOOP or `supervisor_tick()`, C can call a JS import to drain a
JS-side event queue:

```c
// C calls this during supervisor_tick()
__attribute__((import_module("port"), import_name("drainHwEvents")))
extern uint32_t port_drain_hw_events(void);
```

This is polling from C's perspective but calling into JS — different from
the linear-memory event ring (which requires JS to have had a chance to
write into it).  Both paths are complementary:

- **Event ring**: for events JS wrote between frames (already in memory)
- **WASM import drain**: for events JS queued but hasn't written yet

### (b) Module notification on pin change

When `SH_EVT_HW_CHANGE` fires and writes a pin value to MEMFS, nothing
tells common-hal modules that care about specific pins.  On real boards,
the GPIO ISR calls `keypad_tick()` directly.

The fix is a **pin-change notification hook** in the event handler:

```c
case SH_EVT_HW_CHANGE: {
    // ... existing MEMFS write + latch logic ...

    // Notify registered modules about this pin change
    hal_notify_pin_change(pin);
    // -> walks a callback list, calls each registered handler
    // -> keypad: scan matrix, queue key event
    // -> countio: increment counter
    // -> digitalio: wake waiting context
}
```

Modules register interest during construct, deregister during deinit.
The notification hook is the WASM equivalent of a GPIO ISR dispatch table.

### (c) Common-hal modules as active participants

Currently, common-hal modules are **passive readers** — they read MEMFS
when Python calls `get_value()`.  On real boards, modules are **active**:
ISRs fire, counters increment, key matrices scan on a timer.

Modules need to register periodic callbacks via `supervisor_tick()`:

```c
// In supervisor_tick() — lightweight, runs at ~1ms
void supervisor_tick(void) {
    #if CIRCUITPY_KEYPAD
    keypad_tick();         // scan matrix, set pending flag
    #endif

    #if CIRCUITPY_COUNTIO
    countio_tick();        // check edge flags, increment counters
    #endif

    background_callback_add(&tick_callback, supervisor_background_tick, NULL);
}
```

This is exactly what upstream does.  The modules need the data, though —
either from MEMFS reads or from the pin-change notification in (b).

---

## The interrupt model

### Real hardware

```
Button press
  -> GPIO hardware detects edge
  -> ISR fires (< 1 microsecond)
  -> ISR: set flag, enqueue callback
  -> Main loop: drain callback, update module state
  -> Python: reads current value
```

### WASM port (current + proposed)

```
User clicks button on board SVG
  -> DOM event fires (between frames, or queued if mid-frame)
  -> Pin listener pushes SH_EVT_HW_CHANGE to event ring
  -> cp_hw_step: sh_drain_event_ring() processes event
     -> writes MEMFS, latches value, sets dirty flag
     -> hal_notify_pin_change(pin)                        [proposed]
        -> module callbacks fire (keypad_tick, etc.)
        -> background_callback_add() if more work needed
  -> VM runs: HOOK_LOOP calls supervisor_tick() at ~1ms
     -> module ticks check dirty flags                    [proposed]
     -> schedule heavyweight work as callback
  -> HOOK_RETURN: drain callbacks (displayio, etc.)
  -> Python: reads current value from MEMFS
```

### The DOM IS the interrupt controller

On real boards:
- Pin claim -> configure GPIO interrupt in NVIC
- Pin deinit -> disable interrupt

On WASM:
- Pin claim -> C calls `port_register_pin_listener(pin)` (WASM import)
  -> JS attaches mousedown/mouseup to board SVG element
- Pin deinit -> C calls `port_unregister_pin_listener(pin)`
  -> JS removes event listeners

The DOM event listener IS the interrupt handler.  Registration = pin claim.
Deregistration = pin deinit.  Click = interrupt firing.  The WASM import
is the interrupt controller configuration register.

---

## SUSPEND as port_idle_until_interrupt

The SUSPEND mechanism (MP_VM_RETURN_SUSPEND) was built for frame-budget
yielding, but it is actually the port's implementation of
`port_idle_until_interrupt`:

| Upstream (ARM)                | WASM port                          |
|-------------------------------|------------------------------------|
| `port_interrupt_after_ticks(N)` | JS timer -> event ring           |
| `port_idle_until_interrupt()` | SUSPEND -> return to JS            |
| CPU WFI instruction           | No rAF request (zero CPU)          |
| ISR fires -> CPU wakes        | JS event -> `wasm_frame()` call    |

The supervisor's polling model (supervisor_tick -> module polling ->
mp_hal_delay_ms busy-wait -> port_idle_until_interrupt) maps directly:

1. `supervisor_tick()` runs at HOOK_LOOP (~1ms) — polls modules
2. `mp_hal_delay_ms()` busy-waits with RUN_BACKGROUND_TASKS
3. When the delay exceeds a frame budget, SUSPEND yields to JS
4. JS runs its event loop, processes timers, handles user input
5. `wasm_frame()` resumes the VM

The SUSPEND mechanism isn't special WASM infrastructure — it's the standard
port contract (`port_idle_until_interrupt`) implemented through the existing
VM yield machinery.

### Why SUSPEND was still necessary

The upstream busy-wait model (`mp_hal_delay_ms` loops calling
`RUN_BACKGROUND_TASKS`) holds the C stack during the entire delay.  On real
boards, WFI just pauses the CPU — the stack is fine.  On WASM, holding the
C stack blocks the JS thread.

SUSPEND solves this by unwinding the VM state onto pystack (heap, not stack)
and returning to JS.  The C stack is ephemeral; MEMFS and pystack are the
persistent state.  This works because:

- The VM is frame-safe by design (bytecode state on pystack at HOOK_LOOP/RETURN)
- Port-supreme: the port owns all memory, C stack is transient
- MEMFS is the substrate — survives C stack unwinding
- thread_init/deinit are the bookends — VM reads in, executes, writes out

---

## WASI signals

wasi-sdk provides signal definitions (SIGTERM, SIGINT, etc.) but no real
delivery mechanism — there's no `kill()`, no signal handler invocation from
the runtime.  However, the definitions exist and could be used as typed
control codes on a multiplexed channel:

```
JS -> stdin (or dedicated control pipe):
  [SIGINT]   -> mp_sched_keyboard_interrupt()
  [SIGTERM]  -> cp_ctrl_d() (soft reboot)
  [SIGUSR1]  -> custom: reload code.py
  [SIGUSR2]  -> custom: dump state
```

This is an alternative to `SH_EVT_*` typed events — using POSIX signal
numbers as the event type vocabulary.  The event ring already serves this
role; WASI signals would be a standardized vocabulary for the same concept.

Not actionable now, but worth noting: if WASI eventually adds signal
delivery (proposals exist), the port's event handling would map naturally.

---

## Multiplexed I/O — one pipe vs many paths

The current architecture has multiple communication paths:
- Event ring (JS->C): typed events in linear memory
- MEMFS (bidirectional): hardware state, pin values
- WASM exports (C->JS): dirty flags, state addresses
- WASM imports (C->JS): pin listeners, time, frame requests

An alternative: collapse JS->C communication into a single multiplexed
stdin-like pipe with typed messages:

```
JS writes:
  [HW_CHANGE, pin=5, value=1]
  [CTRL_C]
  [EXEC, len=42, code...]

C reads and dispatches:
  parse header -> route to handler
```

**Tradeoffs:**
- **Pro**: one path to maintain, standard WASI I/O, works everywhere
- **Con**: byte-stream framing overhead, no random access (vs MEMFS),
  can't write while WASM is executing (same constraint as event ring)

The event ring is already close to this for JS->C.  MEMFS is the state
store (not transport).  The pipe would be transport; MEMFS would be
persistence.  Worth exploring when the number of event types grows beyond
what the ring handles cleanly.

---

## Per-pin MEMFS endpoints

See `hal-gpio-endpoints.md` for the full design.  Summary:

**Current:** `/hal/gpio` is a flat blob, 12 bytes x 64 pins.  One fd,
addressed by offset arithmetic.

**Proposed:** `/hal/gpio/N` per-pin template files (board defaults) +
`/hal/gpio/active/N` live state files (claimed by Python).

**Connection to interrupt model:** The active file's existence IS the
claim.  The pin listener (DOM event handler) is attached when the active
file is created.  Reset = delete active files + remove listeners.
Template files tell JS what widgets to render before Python claims anything.

**Slot compression (implemented):**
- `enabled` absorbs `never_reset` as tri-state (-1/0/1)
- `direction` absorbs `open_drain` (0=input, 1=output, 2=open_drain)
- `role` and `category` both kept: role is runtime (active file),
  category is static (template file)

---

## The frame budget model (current, with refinements)

```
wasm_frame(now_ms, budget_ms=13):

  1. HARDWARE STEP (cp_hw_step)                    ~1ms
     - Drain event ring (SH_EVT_HW_CHANGE, etc.)
     - Process pin changes -> MEMFS write, latch, notify modules
     - RUN_BACKGROUND_TASKS (drain callback queue)
     - Export dirty state for JS

  2. VM BURST (cp_step)                            ~10-11ms
     - Pick context, run bytecode within budget
     - HOOK_LOOP (~us): Ctrl-C, supervisor_tick (lightweight), budget
     - HOOK_RETURN: drain callback queue (heavyweight work)
     - SUSPEND if budget exhausted

  3. RETURN TO JS                                  ~2ms for rendering
     - JS idle time: paint canvas, update UI, process DOM events
     - DOM events fire -> pin listeners push to event ring
     - Ready for next frame
```

The key insight from today: **display refresh should not run at HOOK_LOOP.**
It runs at HOOK_RETURN (function returns) and frame bookends.  The
lightweight/heavyweight split ensures the bytecode hot path stays lean
while modules still get their ~1ms heartbeat for flag checking.

---

## Implementation roadmap

### Done (today)

1. **Event-routed hardware** — SH_EVT_HW_CHANGE replaces direct MEMFS
   writes + dirty flags.  Per-event latching (first-edge-wins).
2. **Pin data consolidation** — pin_meta merged into MEMFS /hal/gpio
   slots with compression.
3. **Lightweight/heavyweight tick split** — supervisor_tick() at HOOK_LOOP
   schedules work; supervisor_background_tick() at drains executes it.
4. **Pin listener PoC** — WASM import for listener registration, DOM
   events fire through event ring to Python.

### Next (near-term)

5. **Pin-change notification hook** — `hal_notify_pin_change(pin)` in
   the SH_EVT_HW_CHANGE handler.  Walk a callback list, notify registered
   modules.  Modules register during construct, deregister during deinit.

6. **Module ticks in supervisor_tick()** — keypad_tick(), countio_tick(),
   etc. in the lightweight tick.  These check dirty flags and update
   internal state.  Matches upstream ISR pattern.

7. **Per-pin MEMFS endpoints** — split flat /hal/gpio into per-pin files.
   Template (board defaults) + active (claimed state).  See
   hal-gpio-endpoints.md.

### Future (architectural)

8. **WASM import event drain** — `port_drain_hw_events()` callable from
   supervisor_tick() during VM execution.  Polls JS-side event queue for
   events that arrived after the frame started.  Complementary to event ring.

9. **port_idle_until_interrupt via SUSPEND** — when mp_hal_delay_ms
   exceeds frame budget, use SUSPEND as the yield-to-JS mechanism.
   Maps 1:1 to upstream's port_idle_until_interrupt contract.

10. **Multiplexed I/O** — if event types proliferate, consider collapsing
    JS->C communication into a single typed message pipe (stdin-like).
    WASI signal codes as the type vocabulary.

11. **Independent scheduling clocks** — VM, display, and UI on separate
    clocks.  Display doesn't stop when VM is idle.  Cursor blinks via
    setInterval.  Board SVG updates on hardware change callbacks.
    See scheduling_policy memory.

---

## Principles

1. **The DOM IS the interrupt controller.**  Pin claim = interrupt
   registration.  Click = interrupt firing.  Deinit = deregistration.

2. **SUSPEND IS port_idle_until_interrupt.**  Not special infrastructure
   — the standard port contract, implemented through VM yield.

3. **MEMFS IS the substrate.**  Hardware state lives in MEMFS, not C
   locals.  Survives SUSPEND, accessible from JS and C.

4. **Supervisor_tick IS the ISR body.**  Lightweight, runs frequently,
   checks flags, schedules heavy work.  Modules slot in the same way
   they do on real boards.

5. **The port runs the hardware; JS provides the physical world.**
   C common-hal modules are active participants (detect edges, maintain
   counters, scan matrices).  JS provides the raw input (DOM events,
   sensor values, time).

6. **Idle time belongs to JS.**  The frame model's purpose is to get
   C work done efficiently and return to JS.  JS has the UI, the event
   loop, and the rendering pipeline.  Don't fight for CPU time — schedule
   work, execute it, leave.
