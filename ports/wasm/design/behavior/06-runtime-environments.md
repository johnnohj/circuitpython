# Runtime Environments

Back to [README](README.md)

The port must behave correctly in three environments.  The core
lifecycle is the same; what differs is how the port interfaces with
the host.

---

## Frame source (who calls chassis_frame)

| Environment | Mechanism | Budget |
|------------|-----------|--------|
| **Node (CLI)** | Child process or explicit step loop | 8ms soft / 10ms firm |
| **Browser (main thread)** | `requestAnimationFrame` or `setTimeout` | 8ms soft / 10ms firm |
| **Web Worker** | `postMessage` from main thread, or internal `setTimeout` | 8ms soft / 10ms firm |

### Budget model (decided)

- **8ms soft deadline**: HOOK_LOOP checks wall clock.  If elapsed >= 8ms,
  triggers `mp_sched_vm_abort()`.  The abort may not land immediately --
  the VM needs to reach a backwards branch and call `mp_handle_pending`.

- **10ms firm deadline**: If we're past 10ms (e.g., the VM launched into
  an iterator or multi-line operation at 7.85ms), abort immediately via
  `mp_handle_pending` with no further bytecode execution.  This is the
  hard stop.

- The 2ms buffer between soft and firm handles the case where we enter
  a slightly-longer-than-average operation near the soft deadline.  We
  may end up over 8ms but remain under 10ms.

- Entry/exit overhead of `chassis_frame` itself should be <1ms, so
  total per-rAF cost is budget + overhead, well under 16.67ms for 60fps.

- These values (8/10) are initial.  We may experiment with shorter
  budgets (5ms soft / 7ms firm) to find the balance between keeping the
  Python VM responsive and maintaining a snappy UI.

### Execution model (decided)

All environments use abort-resume.  Node runs the VM in a child
process (like a worker), not on the main Node event loop.  This gives
us a single execution model across all environments, simplifies
testing, and means Node can be killed/restarted if the VM runs away.

---

## Filesystem

| Environment | Backend | Persistence | CIRCUITPY path |
|------------|---------|-------------|----------------|
| **Node (CLI)** | Real filesystem via WASI | Native disk | Configurable (pwd or specified) |
| **Node (test)** | wasi-memfs (in-memory) | None (ephemeral) | `/CIRCUITPY/` in memfs |
| **Browser** | wasi-memfs + IdbBackend | IndexedDB | `/CIRCUITPY/` in memfs |
| **Web Worker** | wasi-memfs + IdbBackend (via main thread proxy) | IndexedDB | `/CIRCUITPY/` in memfs |

- In all cases, Python sees a POSIX VFS mounted at `/`.
- Node CLI can optionally point at a real directory (like a USB drive).
- Browser persistence survives page reloads but not cache clears.

---

## Serial I/O (decided)

| Environment | RX (input to VM) | TX (output from VM) |
|------------|-------------------|---------------------|
| **Node** | Ring buffer <- JS writes via WASM import | TX ring -> JS reads via WASM export |
| **Browser** | Ring buffer <- DOM keyboard events | TX ring -> JS reads -> DOM element |
| **Web Worker** | Ring buffer <- postMessage from main thread | TX ring -> postMessage to main thread -> DOM |

All environments use the same ring-buffer path.  The intermediate
buffer step is necessary to make data available to the VM at all,
and the once-removed execution model (JS drives the frame loop, C
runs within it) is what enables abort-resume in the first place.

- JS writes input bytes to the RX ring via a WASM import.
- C-side readline reads from the RX ring as a background task.
- C writes output bytes to the TX ring.
- JS drains the TX ring after each frame.

No environment bypasses the ring buffer.  Even Node uses it, via
FFI binds that let JS feed stdin data into the ring and read stdout
data out of it.

---

## Display

| Environment | Mechanism | Notes |
|------------|-----------|-------|
| **Node (CLI)** | None | Standard board has no display |
| **Browser** | OffscreenCanvas or direct Canvas | Framebuffer in port_mem, JS reads RGB565 |
| **Web Worker** | OffscreenCanvas (transferred from main) | Same framebuffer protocol |

- Display is only active on browser boards (`CIRCUITPY_DISPLAYIO`).
- The framebuffer lives in port_mem at a known offset.  JS creates a
  typed array view over it and blits to canvas each frame.

---

## Hardware simulation

| Environment | Source of hardware events | Pin state |
|------------|-------------------------|-----------|
| **Node (CLI)** | None (standard board) or test harness | N/A or MEMFS slots |
| **Browser** | DOM events on board SVG elements | MEMFS `/hal/gpio` slots |
| **Web Worker** | postMessage relay from main thread | MEMFS `/hal/gpio` slots |

- The event ring is the universal input channel.
- In browser, DOM event listeners (button clicks, slider changes) push
  events into the ring.
- In Worker, the main thread relays DOM events via postMessage, which
  the JS host converts to event ring entries.
- The C code doesn't know or care which environment it's in -- it just
  drains the event ring.
- Pull up/pull down state changes come from JS (toggle events) via the
  event ring.  Direction and value logic lives in C (common-hal).

---

## Interrupts and preemption

| Environment | Ctrl-C | Budget preemption | Pin change "interrupt" |
|------------|--------|-------------------|----------------------|
| **Node** | Event ring (from parent process) | `mp_sched_vm_abort()` in HOOK_LOOP | Event ring (from test harness) |
| **Browser** | Keyboard event -> event ring | `mp_sched_vm_abort()` in HOOK_LOOP | DOM event -> event ring -> latch |
| **Web Worker** | postMessage -> event ring | `mp_sched_vm_abort()` in HOOK_LOOP | postMessage -> event ring -> latch |

- Budget preemption is the port's ISR equivalent.  It fires at every
  backwards branch (HOOK_LOOP) when wall-clock budget is exceeded.
- Pin change detection is event-driven: JS pushes a HW_CHANGE event,
  C latches the new value into the MEMFS pin slot during event drain.
- Ctrl-C always goes through `mp_sched_keyboard_interrupt()` regardless
  of environment.

---

## Thread model

| Environment | Execution context | Implications |
|------------|-------------------|--------------|
| **Node** | Child process | Can be killed/restarted by parent |
| **Browser** | Main thread (shared with DOM) | Must not block, ~10ms firm budget |
| **Web Worker** | Dedicated thread | Can be terminated/recreated by main thread |

- `mp_thread_init()` is called before `mp_init()` in all environments.
- `mp_thread_get_state()` returns `&mp_state_ctx.thread` (single thread).
- Atomic sections (`mp_thread_begin/end_atomic_section`) defer abort
  during critical operations (GC, scheduler updates).
- These are NOT no-ops -- they protect against abort firing during
  operations that must be atomic.
