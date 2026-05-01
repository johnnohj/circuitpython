# Workflow Analog — USB/Serial → JS/Browser

**Created**: 2026-04-28
**Updated**: 2026-04-29 — added IO channel separation, packet protocol refs
**Context**: Understanding how upstream's USB/workflow layer maps to
our JS runtime, to inform the REPL/code execution lifecycle.
**See also**: [io-channels.md](io-channels.md) (two-channel architecture),
[packet-protocol.md](packet-protocol.md) (per-frame data contract)

## What USB provides on real boards

On a real CircuitPython board, USB provides three things:

1. **Serial console** — the REPL, print output, Ctrl-C/D
2. **Mass storage** — the CIRCUITPY drive (code.py, lib/, etc.)
3. **HID/MIDI/etc.** — optional device interfaces

The supervisor lifecycle in `main.c` depends on these:

```
main():
  run_boot_py()
  supervisor_workflow_start()    ← starts USB, serial, BLE, web
  loop:
    run_code_py()                ← auto-starts, uses serial_connected()
    run_repl()                   ← blocks on pyexec_friendly_repl()
```

Key functions the lifecycle calls:
- `serial_connected()` — is a serial console attached?
- `serial_bytes_available()` — is the user typing?
- `serial_read()` / `serial_write()` — console I/O
- `usb_connected()` — is the USB cable plugged in?
- `supervisor_workflow_active()` — is ANY workflow channel active?
- `usb_background()` — keep the USB stack alive

## What we provide instead

| USB concept | Our analog | Implementation |
|-------------|-----------|----------------|
| USB cable plug | Page load / `CircuitPython.create()` | JS creates WASM instance |
| Serial console | Browser serial monitor / Node stdout | `port_mem.serial_rx/tx` ring buffers |
| Mass storage (CIRCUITPY) | MEMFS + IndexedDB persistence | `wasi.js` with IdbBackend |
| `serial_connected()` | Always true | JS host is always present |
| `serial_bytes_available()` | Check `serial_rx` ring | Already implemented |
| `usb_background()` | Frame loop (`chassis_frame`) | Already implemented |
| `supervisor_workflow_start()` | `chassis_init()` | Port init = workflow start |
| `supervisor_workflow_active()` | Always true | Browser is always connected |

## The lifecycle gap

The upstream lifecycle is **blocking**:
```c
run_repl():
    start_mp()
    pyexec_friendly_repl()    ← blocks until Ctrl-D
    cleanup_after_vm()
```

`pyexec_friendly_repl()` calls `mp_hal_stdin_rx_chr()` in a loop,
which blocks until a character arrives.  On real hardware, this is
fine — the board has nothing else to do.  USB background tasks run
from interrupts.

On WASM, blocking is fatal — it freezes the browser.  Our
`mp_hal_stdin_rx_chr()` (in `supervisor/micropython.c`) loops with
`wasm_vm_hook_loop()` which checks the budget and aborts back to
JS when time is up.  But `pyexec_friendly_repl()` is a deep call
stack: `run_repl → pyexec_friendly_repl → mp_hal_stdin_rx_chr`.
When the abort fires, that entire stack is destroyed.

## How abort-resume handles this

The abort-resume protocol saves `ip` and `sp` to `code_state` on
pystack before the abort fires (via `MICROPY_VM_HOOK_LOOP`).  But
`pyexec_friendly_repl()` is C code, not Python bytecode — there's
no `code_state` to save.  The REPL loop is not on pystack.

This means we have two options:

### Option 1: Use pyexec_friendly_repl directly

Call it from `step_repl()`.  When `mp_hal_stdin_rx_chr()` aborts,
the nlr_set_abort in `port_frame()` catches it.  Next frame,
`step_repl()` calls `pyexec_friendly_repl()` again — but it starts
from scratch (new prompt, lost input state).

This only works if the REPL input state (partial line, history
position) is stored somewhere persistent, not on the C stack.
Upstream's `pyexec_event_repl_process_char()` is designed exactly
for this — it's a state machine that processes one character at a
time and stores state in static variables.

### Option 2: Character-at-a-time REPL (pyexec_event_repl)

Use `pyexec_event_repl_process_char()` which is the non-blocking
REPL already in CircuitPython.  It processes one character per call
and maintains state in statics.  This is what CIRCUITPY_REPL_EVENT
uses on boards with USB keyboard workflow.

Our `step_repl()` would:
1. Check if serial bytes are available
2. For each byte, call `pyexec_event_repl_process_char(c)`
3. When a complete line is entered, it compiles and executes
4. Execution uses abort-resume normally (bytecode IS on pystack)
5. Return RC_DONE if idle (waiting for input), RC_YIELD if executing

This is the correct approach.  It matches the browser variant's
`MICROPY_REPL_EVENT_DRIVEN (1)` config flag.

### Option 3: C-side readline via background task

Per the behavior spec (04-script-execution.md): keystrokes arrive
as part of the frame packet, a background task feeds them to C-side
readline.  This is essentially Option 2 with our serial rx ring as
the input source.

## What we need to implement

### For REPL:
```c
// port_step.c
static uint32_t step_repl(void) {
    // Feed available serial bytes to the event REPL
    while (serial_bytes_available()) {
        char c = serial_read();
        int result = pyexec_event_repl_process_char(c);
        if (result == PYEXEC_FORCED_EXIT) {
            // Ctrl-D: soft reboot
            port_step_soft_reboot();
            return RC_DONE;
        }
    }
    // If the REPL started executing a line, the VM is running.
    // Abort-resume will yield back here at budget boundaries.
    return RC_DONE;
}
```

### For code.py:
```c
static uint32_t step_code(void) {
    switch (port_mem.state.sub_phase) {
    case SUB_START_MP:
        // VM already initialized in PHASE_INIT
        port_mem.state.sub_phase = SUB_COMPILE;
        return RC_YIELD;

    case SUB_COMPILE: {
        // Compile from input buffer
        mp_code_state_t *cs = cp_compile_file("/code.py");
        if (cs == NULL) {
            port_step_go_idle();
            return RC_DONE;
        }
        // Execute — this will abort-resume at budget boundaries
        mp_call_function_0(MP_OBJ_FROM_PTR(cs));
        // If we reach here, execution completed without abort
        port_mem.state.sub_phase = SUB_CLEANUP;
        return RC_YIELD;
    }

    case SUB_EXECUTE:
        // Resumed after abort — the VM re-enters automatically
        // because code_state is preserved on pystack.
        // This sub_phase may not be needed if abort-resume handles
        // the re-entry transparently through nlr_set_abort.
        return RC_YIELD;

    case SUB_CLEANUP:
        cleanup_after_vm();
        port_step_go_idle();
        return RC_DONE;
    }
}
```

### Workflow stubs needed:

```c
// supervisor/stubs.c (additions)
void supervisor_workflow_start(void) {
    serial_init();  // already a no-op
}

bool supervisor_workflow_active(void) {
    return true;  // JS host is always connected
}

void supervisor_workflow_reset(void) {
    // Nothing to reset — JS manages the connection
}
```

## The key insight

On real boards, `supervisor_workflow_start()` starts the USB stack,
which creates the serial connection, which enables the REPL.
The serial connection is the workflow.

For us, **the page load IS the workflow start**.  The browser tab
IS the serial connection.  `chassis_init()` IS `supervisor_workflow_start()`.
The frame loop IS `usb_background()`.

We don't need to simulate USB.  We need to implement the same
**lifecycle transitions** that USB enables:
1. Connection detected → show banner
2. Code.py exists → run it
3. Code.py finishes → show "press any key"
4. Key pressed → enter REPL
5. Ctrl-D → soft reboot → run code.py again

These transitions are what `index.html`'s Run/REPL/Stop buttons
control.  The JS UI IS the serial terminal.

## IO Architecture — Unified Bus

On real boards, USB is one physical wire carrying multiple logical
streams (CDC serial, MSC storage, HID).  The host polls endpoints
at regular intervals.  The device responds with data from whichever
endpoints have something to say.

Our analog: `port_mem` IS the bus.  `chassis_frame()` IS the poll.
WASM exports ARE control transfers.

| USB concept | Our analog |
|---|---|
| Physical wire | WASM linear memory (`port_mem`) |
| Host poll | `chassis_frame()` call |
| Control transfers | WASM exports (`cp_start_repl`, `cp_ctrl_c`, ...) |
| Bulk/interrupt endpoints | port_mem rings, MEMFS slots, dirty flags |
| Descriptors | `definition.json` |

All data flows through port_mem.  WASI stdin/stdout is diagnostic
only (maps to `console.log`/`console.error`).  No infrastructure
protocol on WASI IO — though it serves as an emergency channel if
the C code runs off the rails (the JS wrapper can detect an
unresponsive frame and kill the Worker).

CLI environments (Node.js, wasmtime) can optionally wire WASI
stdin/stdout as a secondary serial channel via build flags, but
the primary path is always the port_mem rings.

Full design: [io-channels.md](io-channels.md).
Serial fields in the per-frame contract: [packet-protocol.md](packet-protocol.md).

### Implementation path

The upstream `supervisor/shared/serial.c` multiplexer supports
port-specific serial channels via weak `port_serial_*()` functions.
We implement these to read/write the port_mem rings:

```c
port_serial_connected()         -> true
port_serial_bytes_available()   -> serial_rx_available()
port_serial_read()              -> consume from port_mem.serial_rx
port_serial_write_substring()   -> write to port_mem.serial_tx
```

This replaces our current port-local `supervisor/serial.c` (which
conflates board serial with WASI stdout) with the upstream
multiplexer wired to our rings.  See io-channels.md for the full
consolidation plan.
