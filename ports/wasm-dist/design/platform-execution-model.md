# Platform Execution Model

## Principle

The MicroPython VM already has the tools for cooperative, suspendable
execution.  The WASM port's job is to connect them to our platform,
not to hack around them.

## The tools in the VM

### 1. Lexer-directed compilation

`py/lexer.c` tokenizes source into a stream.  The port controls:
- **Where** compiled bytecode is stored (MEMFS-registered heap)
- **How** it's chunked (line-per-token, block-per-indent)
- **What landmarks** are inserted (line-end markers, loop detection)

By hooking the lexer→compiler pipeline, the port can:
- Detect `while True:` and mark the loop body as a schedulable unit
- Insert frame-boundary yield points at line ends or block boundaries
- Create BC_OBJ markers that the scheduler recognizes
- Pre-classify code as "runs to completion" vs "loops" vs "async"

### 2. TRACE_TICK (ip/sp always current)

`DISPATCH()` in `py/vm.c` calls `TRACE_TICK(ip, sp, false)` on every
opcode dispatch.  Without `SYS_SETTRACE`, this is a no-op.  The port
redefines it:

```c
#define TRACE_TICK(current_ip, current_sp, is_exception) do { \
    code_state->ip = (current_ip); \
    code_state->sp = (current_sp); \
} while(0)
```

Two stores per opcode.  ip/sp are always current in code_state.  Any
suspension mechanism can resume correctly — no "only at backwards
branches" limitation.

### 3. Generator/coroutine protocol

`py/objgenerator.c` already implements suspend/resume:
- `gen_resume_and_raise` calls `mp_execute_bytecode(code_state, send_value)`
- On `yield`: saves state, returns to caller, C stack is gone
- On `next()`: rebuilds C stack, resumes from saved ip/sp
- code_state lives on the heap, not the C stack

This IS the reactor pattern.  Each frame calls `next(coro)`.

### 4. Native emit (MICROPY_EMIT_NATIVE)

With native emit, the compiler writes WASM (our "machine code").
Generators compiled natively have their sp index saved as part of
the yield protocol — built into the native calling convention.
We get WASM-native coroutines.

### 5. Thread module (py/modthread.c)

`_thread.exit()` raises `SystemExit`, caught by the thread entry's
NLR handler.  The mechanism shows isolated execution contexts
(`mp_state_thread_t` with own pystack, locals, globals) that can
be created, run, and terminated cleanly.

### 6. Unix-style execution (POSIX path)

The unix port executes `mp_lexer_new_from_file` → `mp_parse` →
`mp_compile` → `mp_call_function_0`.  Blocking, straight through.
In the Worker model, we do exactly this — the Worker blocks, the
main thread stays responsive.

## How they connect

### Worker path (preferred)

```
Worker thread:
  main() {
      mp_init();
      while (1) {
          pyexec_file("code.py");   // blocks until done
          pyexec_friendly_repl();   // blocks on input
      }
  }

  time.sleep(n):
      Atomics.wait(wake, 0, 0, n)  // actually sleeps
      // woken by: timeout, pin change, keystroke

  print("hello"):
      serial_write → port_mem.serial_tx ring
      // main thread reads at 60fps
```

No frame budget.  No abort-resume.  No state machine.  The VM
runs as a normal blocking program.  The main thread reads shared
memory for display/serial/GPIO.

### Main-thread path (fallback, no SAB)

```
Each frame:
  1. Lexer has pre-classified code.py:
     - Setup block (imports, definitions) → runs to completion
     - Main loop body → one iteration per frame as coroutine

  2. The port wraps user code:
     # What user wrote:
     while True:
         pixels[0] = next_color()
         time.sleep(0.1)

     # What the port executes:
     coro = _make_frame_coro(user_code)
     # Each frame:
     next(coro)  # runs one iteration, yields at sleep

  3. time.sleep becomes yield:
     mp_hal_delay_ms → yield to scheduler → resume next frame

  4. TRACE_TICK keeps ip/sp always current:
     If budget expires mid-opcode, abort can resume from
     any point, not just backwards branches.
```

### Hybrid: ctx0 as platform runtime

```
ctx0 (always running, frozen Python):
  import asyncio

  async def run_user_code(path):
      code = compile(open(path).read())
      # Wrap in coroutine, schedule
      task = asyncio.create_task(exec_as_coro(code))
      await task

  async def repl():
      while True:
          line = await async_input(">>> ")
          result = eval(line)
          if result is not None:
              print(result)

  async def main():
      await run_user_code("/CIRCUITPY/boot.py")
      await run_user_code("/CIRCUITPY/code.py")
      await repl()

  # The event loop IS the frame loop
  # Each frame: asyncio.run_once()
  # JS calls: asyncio.step() per rAF
```

## Lexer landmarks

The lexer can mark code structure for the scheduler:

```
BC_FRAME_BOUNDARY     — inserted at line ends (potential yield points)
BC_LOOP_START         — marks beginning of a detected loop body
BC_LOOP_END           — marks end of loop body (natural frame boundary)
BC_IMPORT_BLOCK       — marks import section (run to completion)
BC_ASYNC_CHECKPOINT   — inserted before blocking calls (sleep, I/O)
```

The scheduler reads these landmarks to decide:
- "This import block runs to completion on one frame"
- "This loop body is one frame's work"
- "This sleep call is a yield point — return to JS"

## MEMFS-located code

Compiled bytecode lives in MEMFS-registered heap (port_mem).
This means:
- JS can inspect compiled code (for debugging, profiling)
- The entire execution state (code + code_state + pystack) is in
  port_mem, which is the sync bus
- State is serializable — save/restore sessions, transfer between
  Workers
- The sync bus carries not just hardware state but VM state

## What this replaces

- **abort-resume**: no longer needed — coroutines yield naturally
- **vm_yield (upstream changes)**: no longer needed — the platform
  uses standard MicroPython mechanisms
- **chassis_frame state machine**: no longer needed in Worker path;
  replaced by asyncio.step() in main-thread path
- **Frame budget enforcement**: natural in coroutine model — each
  `next(coro)` does one unit of work and returns
- **HOOK_LOOP ip/sp saves**: replaced by TRACE_TICK — always current
