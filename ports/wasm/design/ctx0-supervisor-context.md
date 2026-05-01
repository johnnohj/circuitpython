# ctx0: Supervisor Context

**Status**: Future direction (design sketch)
**Date**: 2026-04-30
**Depends on**: Option B VM lifecycle (proven), abort-resume (proven), context system (ports/wasm, not yet in wasm-tmp)

## Concept

ctx0 is a Python execution context owned by the supervisor, distinct from
user code contexts.  It runs frozen/trusted Python that manages the runtime
on behalf of user code.

```
┌─────────────────────────────────────────────┐
│  ctx0 — Supervisor Python                   │
│                                             │
│  Always alive.  Never torn down between     │
│  user runs.  Runs frozen code from the      │
│  port's lib/ directory.                     │
│                                             │
│  Owns:                                      │
│    - asyncio event loop (the scheduler)     │
│    - JS↔Python bridge setup                 │
│    - Background tasks                       │
│    - User code lifecycle                    │
└──────────────┬──────────────────────────────┘
               │ manages
┌──────────────▼──────────────────────────────┐
│  ctx1 — User Python                         │
│                                             │
│  Created fresh for each code.py or REPL     │
│  session.  Destroyed when done.             │
│  Execution is mediated by ctx0.             │
└─────────────────────────────────────────────┘
```

## Motivation

### Current model

The C port layer (port_step.c) manages the lifecycle state machine:
idle → code → idle → repl → idle.  Transitions between states are
handled by C functions (do_cleanup_between_runs, pyexec_file, etc.).
The VM is a black box that runs until it finishes or aborts.

This works but limits what's possible:
- No Python-level scheduling between iterations of user code
- No Python-level background tasks during user execution
- No Python-level mediation of JS↔Python communication
- The C supervisor must handle every lifecycle concern

### Proposed model

ctx0 runs an asyncio event loop that IS the supervisor's Python face.
User code becomes one or more asyncio tasks managed by ctx0.

```python
# Frozen supervisor code (ctx0)
import asyncio

async def run_user_code(path):
    """Execute user code as a managed task."""
    code = compile_file(path)
    task = asyncio.create_task(_execute(code))
    await task

async def run_repl():
    """Interactive REPL as an asyncio task."""
    while True:
        line = await readline()
        result = eval_line(line)
        if result is not None:
            print(repr(result))
```

## The `while True:` transformation

Most CircuitPython user code follows this pattern:

```python
while True:
    # read sensors
    # update outputs
    time.sleep(0.1)
```

On a real board, this runs as a blocking loop — the MCU has nothing
else to do.  In the browser, this loop must yield to JS for display
refresh, input handling, and animation.

Currently, abort-resume handles this: the VM runs the loop body until
the budget expires, then aborts mid-execution.  This works but is
coarse — the abort can land anywhere in the loop body.

With ctx0, the transformation becomes:

```python
# ctx0 detects the while True: pattern at compile time
# and transforms it into cooperative iteration:

async def _run_loop_body(body_code):
    while True:
        exec(body_code)      # run one iteration
        await asyncio.sleep(0)  # yield to scheduler

# The "loop" is ctx0 calling the body repeatedly.
# Each iteration is a clean unit of work.
# Frame boundaries fall between iterations, not mid-bytecode.
```

For well-structured code (which most CircuitPython code is), this
eliminates the need for abort-resume entirely.  The abort mechanism
remains as a safety net for tight inner loops or code that doesn't
follow the `while True: ... sleep()` pattern.

## What ctx0 owns

### 1. asyncio event loop

ctx0's event loop is the single scheduler for all Python work.
User code tasks, background tasks, and bridge tasks all run through
it.  The C port layer calls into ctx0 each frame to run pending
tasks.

### 2. JS↔Python bridge

ctx0 establishes the proxies and event contracts between JS and
Python.  When user code imports `board` and accesses `board.D5`,
the underlying proxy was set up by ctx0.  This setup survives
across user code runs because ctx0 is never torn down.

### 3. Background tasks

Some work needs to happen every frame regardless of what user code
is doing: hardware polling, display refresh triggers, serial
processing.  These are asyncio tasks in ctx0 that run alongside
(or interleaved with) user code tasks.

### 4. User code lifecycle

ctx0 compiles, schedules, and monitors user code:
- Compile code.py or REPL input
- Create an asyncio task for execution
- Monitor for completion, exception, or interrupt
- Clean up after the task finishes
- Report results (print traceback, show prompt, etc.)

## Scheduling model

Each frame, the C port layer:
1. Runs port-level work (event drain, HAL step)
2. Calls ctx0's event loop to run pending tasks
3. ctx0 decides what to run: user code iteration, background task,
   bridge work, or nothing (idle)
4. Returns to C, which runs background callbacks (displayio, etc.)

The key insight: **ctx0 answers "what do we run next?"** — not the
C supervisor.  C provides the frame boundaries and abort safety net.
Python provides the scheduling intelligence.

## Relationship to Option B cleanup

Option B (targeted cleanup between runs) is a prerequisite.  ctx0
must survive across user code transitions.  The VM is long-lived,
ctx0 is always alive, and only user contexts are created/destroyed.

The cleanup between runs:
- Release user pins, HAL state (C-level)
- Destroy ctx1 (user context)
- gc_collect() to reclaim ctx1's objects
- ctx0 remains untouched

## Open questions

1. **asyncio in ctx0 vs user code**: If ctx0 owns the event loop,
   can user code also use asyncio?  Probably yes — user tasks are
   scheduled on ctx0's loop.  But this needs careful design to avoid
   interference.

2. **Compile-time loop detection**: How reliable is detecting
   `while True:` patterns?  What about `for item in collection:`?
   Iterator-based loops are naturally cooperative (one iteration
   per step).

3. **Error isolation**: If user code raises an exception, ctx0 must
   catch it, report it, and continue running.  asyncio task
   exception handling provides this naturally.

4. **Performance**: Adding a Python scheduling layer adds overhead
   per frame.  Is the overhead acceptable?  The alternative (C-only
   scheduling) is faster but less flexible.

5. **Frozen code management**: ctx0's code is frozen into the WASM
   binary.  How do we iterate on it during development?  One option:
   load it from MEMFS during development, freeze for production.
