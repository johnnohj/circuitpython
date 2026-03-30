# WASM Semihosting Syscall Table

## Concept

On ARM/RV32, semihosting traps to the debugger via BKPT/EBREAK.
On WASM, JS is the host.  The "trap" is: C writes a binary request
to /sys/call (a MEMFS endpoint), then yields.  JS reads the request,
fulfills it, writes /sys/result.

## MEMFS Endpoints

| Path | Direction | Format | Purpose |
|---|---|---|---|
| `/sys/call` | C → JS | 264-byte binary record | Semihosting request |
| `/sys/result` | JS → C | 264-byte binary record | Host response |
| `/sys/events` | JS → C | 8-byte event entries | Event queue (keys, timers, hw) |
| `/sys/state` | C → JS | 24-byte record | VM state (readable without WASM call) |

## Wire Format

### /sys/call (264 bytes)
```
[call_nr:u32] [status:u32] [arg0:u32] [arg1:u32]
[arg2:u32] [arg3:u32] [payload: 240 bytes]
```

Status: 0=IDLE, 1=PENDING, 2=FULFILLED, 3=ERROR

### /sys/result (264 bytes)
```
[call_nr:u32] [status:u32] [ret0:u32] [ret1:u32]
[payload: 248 bytes]
```

### /sys/events (ring of 8-byte entries)
```
[event_type:u16] [event_data:u16] [arg:u32]
```

### /sys/state (24 bytes)
```
[sup_state:u32] [yield_reason:u32] [yield_arg:u32]
[frame_count:u32] [vm_depth:u32] [pending_call:u32]
```

## Syscall Numbers

### Classic semihosting (0x01–0x31)
Mirrors ARM/RV32 numbering.

| # | Name | Purpose |
|---|---|---|
| 0x01 | SYS_OPEN | Open file on host fs |
| 0x02 | SYS_CLOSE | Close host file handle |
| 0x03 | SYS_WRITEC | Write char to debug console |
| 0x05 | SYS_WRITE | Write buffer to host fd |
| 0x06 | SYS_READ | Read from host fd |
| 0x07 | SYS_READC | Read char from console |
| 0x10 | SYS_CLOCK | Host clock (centiseconds) |
| 0x11 | SYS_TIME | Host time (epoch seconds) |
| 0x13 | SYS_ERRNO | Last host errno |
| 0x18 | SYS_EXIT | Terminate |
| 0x30 | SYS_ELAPSED | Elapsed tick count |
| 0x31 | SYS_TICKFREQ | Tick frequency (Hz) |

### Browser extensions (0x100+)

| # | Name | Args | Notes |
|---|---|---|---|
| 0x100 | TIMER_SET | id, delay_ms | Request callback |
| 0x101 | TIMER_CANCEL | id | Cancel timer |
| 0x110 | FETCH | method, url, body | HTTP fetch (async) |
| 0x111 | FETCH_STATUS | request_id | Poll completion |
| 0x120 | PERSIST_SYNC | — | Flush to IndexedDB |
| 0x121 | PERSIST_LOAD | — | Reload from IndexedDB |
| 0x130 | DISPLAY_SYNC | frame_count | Framebuffer ready |
| 0x140 | HW_IRQ | irq_nr, pin | Simulated interrupt |
| 0x141 | HW_POLL | hal_type | Refresh /hal/ endpoint |
| 0x150 | VM_STATE | — | Export to /sys/state |
| 0x151 | VM_EXCEPTION | — | Report exception (payload) |
| 0x1F0 | DEBUG_LOG | level | Write to console.log |

### Event Types (JS → Python via /sys/events)

| # | Name | data | arg |
|---|---|---|---|
| 0x01 | KEY_DOWN | keycode | modifiers |
| 0x02 | KEY_UP | keycode | modifiers |
| 0x10 | TIMER_FIRE | timer_id | 0 |
| 0x11 | FETCH_DONE | request_id | http_status |
| 0x20 | HW_CHANGE | hal_type | pin/channel |
| 0x30 | PERSIST_DONE | 0 | 0 |
| 0x40 | RESIZE | width | height |

## Two Modes

**Synchronous** — for fast host operations (clock, errno).
JS MEMFS callback handles inline during fd_write.
C reads /sys/result immediately, no yield needed.

**Asynchronous** — for operations needing real async work (fetch, timer, I2C).
C writes /sys/call, yields YIELD_IO_WAIT.
JS fulfills at its leisure, writes /sys/result.
Supervisor polls sh_poll() on next cp_step().

## Two-Tier I/O Model

WASI fd operations (fd_write, fd_read, fd_seek) are unsafe for per-frame use.
When memory.grow() is called, it detaches WebAssembly.Memory.buffer, invalidating
any DataView or Uint8Array that JS fd handlers created from it earlier in the frame.
Reads and writes to the stale buffer silently corrupt data.

This produces a two-tier split:

### Hot path — direct linear memory (per-frame)

JS accesses WASM linear memory directly through exported C pointers. No fd
operations, no DataView creation from a potentially detached buffer.

| Export | Direction | Layout | Purpose |
|---|---|---|---|
| `sh_state_addr()` | C → JS | 24-byte record (see /sys/state above) | JS reads VM state directly from WASM memory |
| `sh_event_ring_addr()` | JS → C | `[write_idx:u32] [read_idx:u32] [entries: N * sh_event_t]` | JS writes events directly into a ring buffer in WASM memory |

JS re-creates its DataView from `instance.exports.memory.buffer` at the start of
each frame (after any possible memory.grow), then reads/writes at the known offsets.
No WASI fd calls are involved.

### Cold path — WASI fds (init-time, low-frequency)

WASI fd operations remain safe for infrequent calls where memory.grow() is not
expected to interleave: init-time filesystem setup, semihosting requests (fetch,
timer, persist), and /hal/ endpoint updates. These can afford the overhead of
fresh DataView creation per call.

## Implementation Files

- `supervisor/semihosting.h` — C header with syscall table, structs, API
- `supervisor/semihosting.c` — C implementation (fd management, call/poll/result)
- `js/semihosting.js` — JS handler (dispatch, sync/async fulfillment, events)
