CircuitPython WASM port
=====================

### LLM Disclosure

This port has been created over several iterations through the use of an agentic LLM - Claude Code. The overall designs and larger architectural choices are mine, informed by an amateur's understanding of the designs and choices made by existing ports, in particular the `unix` and MicroPython's `webassembly` ports. While development has been a sustained effort, rather than a one-and-done prompt session, the hazards and pitfalls inherent to the use of LLM programs nevertheless persist. This work is offered to the community 'warts and all.'

### Project Goals

If you'll permit a short indulgence, this project was undertaken as a personal hobby project; it's been as much about seeing CircuitPython/MicroPython from 'inside' as about trying to build something useful or, at least, interesting. Based on those criteria, it's already been a success.

But, it's not done.

As the `README.md` file notes:
    This port is offered for rapid prototyping, general testing, and experimentation with the primary intention to allow users to see whether some code 'works' even when they happen not to have the hardware at hand.
Indeed, the guiding mantra has been `If it works on real hardware, it should work here.`

Most ports, however, don't need to provide their own hardware or, more specifically, hardware representations. While that hardware may be simple JavaScript objects, the departure from established scope for this port is already enough to give me pause. I would be honored to have this port, or something this work inspired, be adopted officially, potentially as a part of the `circuitpython.org` online editor, as previous iterations have experimented with, but whether and how are not for me to say.

It remains to be said that web assembly is very much an emerging technology, with standards under active development and review. This port can and should only be considered experimental for that reason. If, however, it illuminates the possibilities for running CircuitPython in a web browser and inspires sustained interest, that is enough.

### Features, Current and Envisioned

- Simulated board behavior with familiar boot, execution cycle
- Genuine CircuitPython REPL experience
- Board hardware simulated via JavaScript and HTML, operated by CircuitPython
- Sensor mocking with data routed to the browser 'board,' a real board, or both
- Blinka-style interaction with a real board/hardware using the browser 'board'

## Outstanding Work

See `MIGRATION_PLAN.md`

### Design Commentary

Principal challenges:
- Fidelity to real hardware/expected behavior
- Constraints of the JavaScript + WebAssembly runtime environment
- Inventing what a wasm port of CircuitPython might look like
