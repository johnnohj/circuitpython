# CircuitPython WebAssembly Port Architecture Recommendations

## Executive Summary

The current CircuitPython WebAssembly port suffers from memory access errors, inefficient JavaScript integration, and architectural tensions between hardware-oriented CircuitPython design and the browser environment. This document proposes solutions.

## Core Issues and Solutions

### 1. Memory Access Error Fix

**Problem**: Board module import crashes due to premature JavaScript proxy access.

**Immediate Fix**:
```c
// In board.c, add initialization guard:
void board_init(void) {
    if (!proxy_c_is_initialized()) {
        return; // Skip until proxy system ready
    }
    // ... rest of initialization
}
```

**Long-term Fix**: Lazy-load board configuration from JavaScript after full initialization.

### 2. Modular Architecture Proposal

```
circuitpython-core.wasm (200KB)
├── Python interpreter
├── Basic types
└── Core runtime

circuitpython-stdlib.wasm (300KB)
├── Standard library
├── Collections
└── Math modules

circuitpython-hardware.wasm (100KB)
├── board module
├── digitalio
└── Pin abstraction

circuitpython-filesystem.wasm (150KB)
├── VFS layer
├── Storage module
└── File operations
```

### 3. Improved JavaScript Integration

```javascript
// Use SharedArrayBuffer for efficient data exchange
class CircuitPythonBridge {
    constructor() {
        this.sharedMemory = new SharedArrayBuffer(1024 * 1024);
        this.inputBuffer = new Uint8Array(this.sharedMemory, 0, 4096);
        this.outputBuffer = new Uint8Array(this.sharedMemory, 4096, 4096);
    }
    
    async executeCode(code) {
        // Write code to shared buffer
        // Signal WASM to process
        // Await result in output buffer
    }
}
```

### 4. REPL Architecture

```javascript
// Use Web Worker for non-blocking REPL
// repl-worker.js
self.importScripts('circuitpython-core.js');

const repl = new CircuitPythonREPL();
self.onmessage = async (e) => {
    const result = await repl.execute(e.data);
    self.postMessage(result);
};
```

### 5. Build System Improvements

```makefile
# Separate build targets
.PHONY: core stdlib hardware filesystem

core:
	$(EMCC) $(CORE_SOURCES) -o circuitpython-core.wasm \
		-s SIDE_MODULE=1 -s EXPORT_ALL=1

stdlib: core
	$(EMCC) $(STDLIB_SOURCES) -o circuitpython-stdlib.wasm \
		-s MAIN_MODULE=1 -s RUNTIME_LINKED_LIBS="['circuitpython-core.wasm']"

# Enable dynamic linking
JSFLAGS += -s MAIN_MODULE=2  # For main module
JSFLAGS += -s SIDE_MODULE=2  # For side modules
```

## Migration Path

### Phase 1: Fix Critical Issues (Week 1)
- Add QSTR definitions ✓
- Fix board initialization guards
- Add proxy system initialization checks

### Phase 2: Improve JavaScript Bridge (Week 2-3)
- Implement SharedArrayBuffer communication
- Add Web Worker support
- Create async/await wrapper API

### Phase 3: Modularize Architecture (Week 4-6)
- Split monolithic build into modules
- Implement dynamic module loading
- Create module dependency system

### Phase 4: Optimize Performance (Week 7-8)
- Enable WASM SIMD operations
- Implement JIT caching
- Add IndexedDB persistence

## Performance Targets

- Initial load: < 500ms (currently ~2s)
- REPL response: < 50ms (currently ~200ms)
- Module import: < 100ms (currently crashes)
- Memory usage: < 10MB baseline (currently ~20MB)

## Compatibility Considerations

### What to Keep from CircuitPython
- Python 3.x syntax compatibility
- Core module API signatures
- REPL behavior and error messages

### What to Adapt for WebAssembly
- Replace hardware pins with virtual pins
- Use JavaScript events instead of interrupts
- Implement virtual filesystem in IndexedDB
- Map board module to browser capabilities

## Testing Strategy

```javascript
// Comprehensive test suite
describe('CircuitPython WASM', () => {
    it('should import board without crashing', async () => {
        const cp = await CircuitPython.init();
        const result = await cp.import('board');
        expect(result).toBeDefined();
    });
    
    it('should handle async operations', async () => {
        const cp = await CircuitPython.init();
        const result = await cp.execute('import asyncio');
        expect(result.error).toBeUndefined();
    });
});
```

## Conclusion

The CircuitPython WebAssembly port requires architectural changes to work effectively in the browser environment. The proposed modular architecture, improved JavaScript integration, and build optimizations will resolve current issues while maintaining CircuitPython compatibility.

The key insight is that CircuitPython's hardware-centric design needs adaptation layers for the web platform, not direct compilation. This approach preserves CircuitPython's educational value while enabling browser-based learning and development.