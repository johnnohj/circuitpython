# Acceptance Criteria

Back to [README](README.md)

A correctly-behaving port passes these tests.

---

## Lifecycle correctness

- [ ] `import sys; print(sys.path)` shows expected paths
- [ ] Port initializes and idles (no code runs until user action)
- [ ] "Run" compiles and executes code from the editor
- [ ] "Save" persists code to IDBFS; auto-reload re-runs if enabled
- [ ] `boot.py` runs before `code.py` when code.py is executed
- [ ] Ctrl-C interrupts running code and drops to REPL
- [ ] Ctrl-D from REPL soft-reboots -> returns to idle
- [ ] `cleanup_after_vm` releases all pins (pin state reverts to
      defaults between code.py runs)

---

## Hardware simulation correctness

- [ ] `digitalio.DigitalInOut(board.D5).value = True` updates MEMFS
      pin slot, JS reads change
- [ ] JS button click -> MEMFS pin slot update -> Python reads new value
- [ ] Pin claims are exclusive (second claim raises)
- [ ] Pins are released on soft reboot
- [ ] Pull up/down toggle from JS -> event ring -> C latches new state

---

## Timing correctness

- [ ] `time.sleep(1)` pauses Python bytecode for ~1 second (absolute
      wall-clock duration, not frame-relative)
- [ ] During `time.sleep()`, hardware/UI updates still fire (display
      refreshes, pin changes latched, background tasks run) -- only
      Python bytecode execution pauses
- [ ] `while True: pass` doesn't freeze the browser (budget preemption)
- [ ] asyncio tasks interleave correctly (time.sleep yields, tasks swap)
- [ ] Frame budget soft deadline (8ms) respected; firm deadline (10ms)
      never exceeded except in pathological cases
- [ ] Python timing is absolute: a 1-second `time.sleep` or print
      interval should count off real wall-clock seconds

---

## Environment correctness

- [ ] Node: REPL works via child process + ring buffer
- [ ] Browser: REPL works (keystrokes -> frame packet -> C readline)
- [ ] Browser: display updates visible on canvas
- [ ] Ctrl-C works in all environments
- [ ] Tab completion works in all environments (C-side readline)
