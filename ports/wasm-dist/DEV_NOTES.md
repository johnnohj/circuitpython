CircuitPython WASM port
=====================

### Project Goals

As the `README.md` file notes:
    This port is offered for rapid prototyping, general testing, and experimentation with the primary intention to allow users to see whether some code 'works' even when they happen not to have the hardware at hand.
Indeed, the guiding mantra has been `If it works on real hardware, it should work here`.

### Features, Current and Planned

- Simulated board behavior with familiar boot, execution cycle
- Genuine CircuitPython REPL experience
- Board hardware simulated via JavaScript and HTML, operated by CircuitPython
- Sensor mocking with data routed to the browser 'board,' a real board, or both
- Blinka-style interaction with a real board/hardware using the browser 'board'

## Outstanding Work

# Lifecycle Scaffolding (small, deferred)

3. Auto-reload via VFS write hook — MEMFS write interception → supervisor notification → re-run code.py. The largest of the three.

# Runtime Behavior

6. boot.py yield-stepping — currently runs blocking via pyexec_file_if_exists, can't yield. Low priority unless boot.py gets heavy.

# Hardware Module Enrichment (builds on today's work)

8. Interactive board SVGs — swappable *.svg board artwork with clickable LEDs, toggleable pins, pressable buttons. Hardware modules drive the SVG state.

# Multi-Context / Scheduling

13. Tab backgrounding — switch to idle context on visibilitychange, resume when visible. Pause/resume exists but no idle-context swap.
14. Sequential file execution — boot.py → code.py → REPL chain through yield-stepping (partially done, boot.py is blocking).

# External Hardware Targets (future)

15. WebUSBTarget — route U2IF commands to real U2IF firmware boards via WebUSB.
16. WebSerialTarget — route commands to real CircuitPython boards via WebSerial raw REPL.
17. Tee mode — simultaneous simulated + real hardware.

# Documentation

18. ARCHITECTURE.md update — needs update for single-worker model, hardware module system, asyncio yield path.

## Rough priority tiers:

- Now ready: 8 (hardware module enrichment — the new system is in place, these build directly on it)
- Next: 6 (runtime edge cases), 3 (lifecycle polish)
- Later: 13-14 (multi-context), 15-17 (external hardware)
- Ongoing: 18 (docs)

### Design Commentary

Principal challenges:
- Fidelity to real hardware/expected behavior
- Constraints of the JavaScript + WebAssembly runtime environment
- Inventing what a wasm port of CircuitPython would look like
