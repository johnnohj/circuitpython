# CircuitPython Browser Port

This port provides a lightweight CircuitPython implementation optimized for web browsers, focusing on educational use and web-based development environments.

## Architecture

- **Target**: Web browsers only (for Node.js, see the separate wasm-node port)
- **Size**: Optimized for fast loading (~250KB total)
- **REPL**: Browser-based interactive Python environment
- **Hardware**: Virtual GPIO through JavaScript HAL provider

## Features

- **Minimal footprint** for fast browser loading
- **Clean Python REPL** without unnecessary complexity
- **Educational focus** with clear, simple APIs
- **Web editor integration** ready

## Build

```bash
make            # Build for browser
make browser    # Build and package for browser deployment
make clean      # Clean build artifacts
```

## Key Features

- **CircuitPython API Compatibility**: Uses standard module names (json, binascii, digitalio, etc.)
- **Proven REPL**: MicroPython's event-driven REPL system
- **Hardware Abstraction**: HAL providers handle platform-specific implementation
- **Node.js Ready**: Character-by-character processing for interactive REPLs
- **Size Efficient**: ~300-400KB target

## Original MicroPython WebAssembly Documentation

This port is based on the MicroPython WebAssembly port. For original MicroPython WebAssembly usage, see the `micropython.mjs` API documentation.
