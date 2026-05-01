# Open Questions

Back to [README](README.md)

Decisions resolved and remaining questions.

---

## Resolved

### 1. Should Node CLI mode use abort-resume or blocking?

**Decision**: Abort-resume everywhere.  Node runs the VM in a child
process (like a worker), not on the main Node event loop.  This gives
us a single execution model to test and maintain, proves abort-resume
works in all contexts, and means Node can be killed/restarted if the
VM runs away -- just like terminating a Web Worker.

### 2. Should we support settings.toml?

**Decision**: Yes, with preset options.  Provide one or two heap size
choices relative to the default (e.g., one larger, one smaller, or two
larger tiers).  No need for fully customizable arbitrary values.

### 3. How should the context system interact with boot.py/code.py sequencing?

**Decision**: The context machinery is retained because it does no harm,
but it's currently a solution looking for a problem.  The supervisor
owns boot.py -> code.py -> REPL sequencing (as upstream does).  The
context system is a lower-level primitive the supervisor can use
internally.  JS does not drive context creation directly for the
standard lifecycle.

### 4. What is our watchdog story?

**Decision**: If running in a Worker or Node child process, the parent
can kill and recreate it if it stops responding.  On the main browser
thread, a runaway C loop (not bytecode -- bytecode is caught by
HOOK_LOOP budget checks) means the user must close the tab.  We could
theoretically tear down the WASM module instance with the right
bindings, but this is an edge case not worth over-engineering for now.

---

## Still open

### 5. Bus fidelity at the pin level

Buses are sets of pins, defined on the `board` module.  We supply
minimal pin objects to satisfy the C build and use JS-sent packet data
under the hood (this is largely what we do already).  The concern is
that if anyone tries to follow an I2C/SPI protocol by reading individual
pins directly, it won't work -- buses operate at a higher abstraction
level than GPIO state.

**Current approach**: Provide full GPIO pin slots so we get built-in
claim logic for free.  Bus operations go through common-hal/busio/
which talks to JS-side component simulations, not through individual
pin reads/writes.

**Status**: May require experimentation/testing to determine the best
path.  Not blocking for migration -- common-hal/busio/ can be brought
over as-is (Phase 5) and refined later.

### 6. boot.py / code.py VM isolation

Upstream tears down the GC between boot.py and code.py, but boot.py
can set supervisor-level configs that affect code.py's abilities --
e.g., setting `storage` to readonly.  So there IS persistence, just
at the supervisor/firmware level, not the Python-globals level.

**Our model**: Follow upstream.  boot.py's effects persist through
common-hal-level state (the `supervisor` or other appropriate Python module
writes to port-level HAL or common-hal config that survives VM teardown).
Python globals do NOT carry over.  This matches upstream and prevents subtle
state leakage.

Decision deferred to Phase 4 for implementation, but the direction
is clear: hardware- or firmware-level persistence, not Python-level.

### 7. Pin pull up/down state updates

Resolved in previous design sessions.  The answer is "both/and":

- JS updates the MEMFS endpoint directly via FFI event/proxy
  (automatic, no polling needed)
- Python reads the endpoint when it needs to (on-demand)
- C needn't poll because the MEMFS slot is already current by the
  time Python asks for the value

The details are documented in the event-driven HAL and FFI event
listener design docs.  Not a migration blocker.
