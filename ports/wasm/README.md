# CircuitPython WebAssembly Port

This port combines the efficiency of MicroPython's WebAssembly implementation with CircuitPython's hardware abstraction and educational API design.

## Architecture

- **Base**: MicroPython WebAssembly runtime (proven, efficient)
- **Compatibility**: CircuitPython module names and APIs
- **Hardware**: HAL provider system for pluggable hardware backends
- **REPL**: Character-by-character event-driven REPL for Node.js

## Variants

- `variants/standard`: MicroPython base + CircuitPython APIs + JavaScript HAL provider

## Providers

- `providers/js_provider.c`: JavaScript hardware provider for browser/Node.js
- `providers/stub_provider.c`: No-op provider for testing
- `providers/sim_provider.c`: Simulation provider with virtual hardware

## Build Targets

```bash
make VARIANT=standard    # Standard CircuitPython-compatible build
```

## Key Features

- **CircuitPython API Compatibility**: Uses standard module names (json, binascii, digitalio, etc.)
- **Proven REPL**: MicroPython's event-driven REPL system
- **Hardware Abstraction**: HAL providers handle platform-specific implementation
- **Node.js Ready**: Character-by-character processing for interactive REPLs
- **Size Efficient**: ~300-400KB target

## Original MicroPython WebAssembly Documentation

This port is based on the MicroPython WebAssembly port. For original MicroPython WebAssembly usage, see the `micropython.mjs` API documentation.
