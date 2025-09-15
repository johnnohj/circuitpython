# Layer 1 Minimal Variant: Required Changes Analysis

## Current State Assessment

Based on your Phase 1 completion report and architecture documents, the current WebAssembly port includes:

**✅ Currently Working (Keep for Layer 1)**:
- MicroPython VM core with CircuitPython extensions
- Standard library modules (sys, gc, math, collections, json)
- REPL functionality with proper initialization
- Module import system (working after Phase 1 fixes)
- Memory management and garbage collection
- Exception handling and error reporting

**❌ Currently Included (Remove for Layer 1)**:
- Hardware simulation modules (digitalio, analogio, busio)
- board module with JavaScript pin definitions
- Hardware abstraction layer (common-hal implementations)
- JavaScript bridge integration (proxy_c system)
- Browser/Node.js specific integration code

## Required Changes for Layer 1 Minimal

### 1. Create New Minimal Variant Structure

```bash
# New directory structure needed
ports/webassembly/variants/minimal/
├── mpconfigvariant.mk     # Minimal module configuration
├── mpconfigvariant.h      # Minimal feature flags
└── README.md              # Variant documentation
```

### 2. Module Configuration Changes

**File: `variants/minimal/mpconfigvariant.mk`**

```makefile
# CircuitPython WebAssembly Minimal Variant
# Layer 1: Pure Python interpreter only

# Inherit minimal settings
CIRCUITPY_FULL_BUILD = 0
LONGINT_IMPL = NONE

# Core Python modules (KEEP)
CIRCUITPY_MATH = 1
CIRCUITPY_COLLECTIONS = 1
CIRCUITPY_JSON = 1
CIRCUITPY_STRUCT = 1
CIRCUITPY_BINASCII = 1
CIRCUITPY_ERRNO = 1
CIRCUITPY_TIME = 1
CIRCUITPY_RANDOM = 1

# System modules (KEEP)
CIRCUITPY_OS = 1
CIRCUITPY_STORAGE = 1
CIRCUITPY_MICROCONTROLLER = 1

# Hardware modules (REMOVE)
CIRCUITPY_DIGITALIO = 0
CIRCUITPY_ANALOGIO = 0
CIRCUITPY_BUSIO = 0
CIRCUITPY_BOARD = 0

# All other hardware modules (REMOVE)
CIRCUITPY_AUDIOBUSIO = 0
CIRCUITPY_AUDIOIO = 0
CIRCUITPY_PULSEIO = 0
CIRCUITPY_PWMIO = 0
CIRCUITPY_ROTARYIO = 0
CIRCUITPY_RTC = 0
CIRCUITPY_SDCARDIO = 0
CIRCUITPY_NEOPIXEL_WRITE = 0
CIRCUITPY_BITBANGIO = 0
CIRCUITPY_PIXELBUF = 0
CIRCUITPY_DISPLAYIO = 0
CIRCUITPY_FRAMEBUFFERIO = 0

# No frozen libraries for minimal build
FROZEN_MPY_DIRS =

# No JavaScript source files
SRC_JS =

# Minimal LDFLAGS (remove proxy exports)
LDFLAGS += -s EXPORTED_RUNTIME_METHODS="ccall,cwrap,getValue,setValue,stringToUTF8,UTF8ToString"
```

### 3. Feature Flag Changes

**File: `variants/minimal/mpconfigvariant.h`**

```c
// CircuitPython WebAssembly Minimal Variant Features

// Disable hardware-specific features
#define CIRCUITPY_MINIMAL_VARIANT 1

// Disable JavaScript integration
#define MICROPY_PROXY_C_SUPPORT 0
#define MICROPY_JS_BRIDGE_SUPPORT 0

// Disable hardware pin support  
#define MICROPY_HW_BOARD_PINS 0
#define MICROPY_HW_PIN_FUNCTION 0

// Keep essential Python features
#define MICROPY_PY_SYS_PATH_ARGV_DEFAULTS 1
#define MICROPY_PY_SYS_EXIT 1
#define MICROPY_PY_SYS_ATEXIT 1

// Memory optimization for minimal build
#define MICROPY_HEAP_SIZE_DEFAULT (64 * 1024)  // 64KB instead of 128KB
#define MICROPY_ALLOC_GC_STACK_SIZE (64)       // Smaller GC stack
```

### 4. Build System Modifications

**File: `Makefile` (modifications needed)**

```makefile
# Add variant-specific build target
.PHONY: minimal

minimal:
	$(MAKE) VARIANT=minimal

# Conditional source inclusion based on variant
ifeq ($(VARIANT),minimal)
    # Exclude JavaScript bridge sources
    SRC_C := $(filter-out proxy_c.c proxy_wrapper.c board.c, $(SRC_C))
    
    # Exclude hardware common-hal implementations
    SRC_C := $(filter-out $(wildcard common-hal/*/*.c), $(SRC_C))
    
    # Minimal supervisor implementation
    SRC_SUPERVISOR := supervisor/port.c supervisor/serial.c
else
    # Standard build includes everything
    SRC_SUPERVISOR := $(wildcard supervisor/*.c)
endif
```

### 5. Source Code Changes

**File: `main.c` (conditional compilation)**

```c
// Add conditional compilation guards
#ifndef CIRCUITPY_MINIMAL_VARIANT

// Existing JavaScript bridge initialization
extern void proxy_c_init_safe(void);
extern bool proxy_c_is_initialized(void);

void mp_js_post_init(void) {
    proxy_c_init_safe();
    if (proxy_c_is_initialized()) {
        board_init();
    }
}

#else

// Minimal variant: no JavaScript bridge
void mp_js_post_init(void) {
    // Nothing to do for minimal variant
}

#endif // CIRCUITPY_MINIMAL_VARIANT
```

**File: `supervisor/port.c` (minimal supervisor)**

```c
#ifdef CIRCUITPY_MINIMAL_VARIANT

// Minimal supervisor implementation
safe_mode_t port_init(void) {
    return NO_SAFE_MODE;
}

void reset_port(void) {
    // Minimal reset
}

void reset_to_bootloader(void) {
    // Not supported in minimal variant
}

bool port_has_fixed_stack(void) {
    return false;
}

#endif // CIRCUITPY_MINIMAL_VARIANT
```

### 6. Remove Hardware Dependencies

**Current dependencies to eliminate:**

```c
// In mpconfigport.h - wrap in #ifndef CIRCUITPY_MINIMAL_VARIANT
#ifndef CIRCUITPY_MINIMAL_VARIANT
// Hardware pin definitions
extern const mp_obj_type_t board_pin_type;
extern const mp_obj_type_t mcu_pin_type;

// Hardware module references
extern const struct _mp_obj_module_t board_module;
extern const struct _mp_obj_module_t digitalio_module;
extern const struct _mp_obj_module_t analogio_module;
extern const struct _mp_obj_module_t busio_module;
#endif
```

### 7. Build Validation Changes

**Update existing test suite for minimal variant:**

```javascript
// test/test_minimal_variant.js
async function testMinimalVariant() {
    console.log('Testing CircuitPython Minimal Variant...');
    
    const circuitpython = await import('../build-minimal/circuitpython.mjs');
    const Module = await circuitpython.default();
    
    // Test 1: Basic Python execution
    const result1 = Module._mp_js_do_str("print('Hello from minimal variant!')");
    console.assert(result1 === 0, 'Basic Python execution failed');
    
    // Test 2: Standard library imports
    const result2 = Module._mp_js_do_str("import sys; import gc; import math");
    console.assert(result2 === 0, 'Standard library imports failed');
    
    // Test 3: Verify hardware modules are NOT available
    const result3 = Module._mp_js_do_str("try:\n    import board\n    exit(1)\nexcept ImportError:\n    pass");
    console.assert(result3 === 0, 'Hardware modules should not be available');
    
    console.log('✅ All minimal variant tests passed!');
}
```

## Size and Performance Targets

### Expected Improvements from Layer 1:

| Metric | Current Standard | Target Minimal | Improvement |
|--------|------------------|----------------|-------------|
| **WASM Size** | ~1.48MB | ~200-300KB | 75-85% reduction |
| **JavaScript Size** | ~250KB | ~10KB | 95% reduction |
| **Memory Usage** | 16MB heap | 4-8MB heap | 50-75% reduction |
| **Load Time** | ~500ms | ~100ms | 80% reduction |

### Functionality Verification:

**✅ Should Work:**
- `print("Hello World")`
- `import sys; print(sys.version)`
- `import math; print(math.pi)`
- `import gc; gc.collect()`
- `import json; json.dumps({"test": "data"})`
- REPL interactive sessions
- Exception handling and traceback display
- Basic arithmetic and string operations

**❌ Should Fail (as expected):**
- `import board`
- `import digitalio`
- `import analogio`
- `import busio`
- Any hardware-related operations

## Implementation Timeline

### Phase 1: Variant Structure (2-3 hours)
1. Create `variants/minimal/` directory structure
2. Add minimal configuration files
3. Modify Makefile for variant builds
4. Test build system works

### Phase 2: Source Modifications (3-4 hours)
1. Add conditional compilation guards
2. Create minimal supervisor implementation
3. Remove hardware dependencies
4. Update main.c initialization

### Phase 3: Testing and Validation (1-2 hours)
1. Build minimal variant
2. Run test suite
3. Verify size targets met
4. Document any remaining issues

**Total Estimated Time: 6-9 hours**

## Success Criteria

**Build Success:**
- ✅ `make VARIANT=minimal` completes without errors
- ✅ Output size is <300KB WASM + <10KB JS
- ✅ No hardware module dependencies in binary

**Runtime Success:**
- ✅ All Python standard library features work
- ✅ REPL starts and operates normally
- ✅ Module imports work for included modules
- ✅ Hardware imports fail gracefully
- ✅ No JavaScript runtime errors

This minimal variant provides the foundation for your Layer 1 pure interpreter, eliminating the architectural complexity while maintaining full CircuitPython API compatibility for supported modules.

---

## INTEGRATION PLAN UPDATE: Selective Integration with minimal-interpreter

**Date**: 2025-09-07  
**Status**: Recommended approach for existing minimal-interpreter variant

### Analysis: Current minimal-interpreter vs Layer 1 Proposal

The existing `minimal-interpreter` variant is actually **more minimal** than the proposed Layer 1 changes:

| Aspect | Current minimal-interpreter | Layer 1 Proposal | Recommended Integration |
|--------|---------------------------|------------------|----------------------|
| **Config Style** | Pure MicroPython flags | CircuitPython `CIRCUITPY_*` flags | **Keep MicroPython** (lighter) |
| **Size Target** | ~150KB (stated) | 200-300KB | **Target <150KB** (more aggressive) |
| **Module System** | Disable CircuitPython modules | Keep some CircuitPython modules | **Keep disabled** (truly minimal) |
| **Build System** | Missing `.mk` file | Complete `.mk` integration | **Add minimal `.mk`** only |

### Selective Integration Strategy

**Phase 1: Essential Build System (30 minutes)**
- ✅ Add `mpconfigvariant.mk` with size optimizations only
- ✅ Integrate memory reduction settings from Layer 1
- ✅ Remove proxy system references
- ❌ Skip CircuitPython module configuration (too heavy)

**Phase 2: Validation (15 minutes)**
- Test `make VARIANT=minimal-interpreter` 
- Verify size <150KB WASM (better than Layer 1's 200-300KB target)
- Ensure core Python functionality works

**Phase 3: Documentation (15 minutes)**
- Document integration approach and rationale

### Implementation Details

**File: `variants/minimal-interpreter/mpconfigvariant.mk` (NEW)**
```makefile
# Minimal interpreter - optimized for smallest possible size
# Target: <150KB WASM (more aggressive than Layer 1's 200-300KB)

# Integer optimization for size
LONGINT_IMPL = NONE

# Memory optimization (more aggressive than standard)
LDFLAGS += -s INITIAL_MEMORY=8MB    # Reduced from Layer 1's 16MB
LDFLAGS += -s STACK_SIZE=256KB      # Reduced from Layer 1's 512KB

# Remove all JavaScript sources (pure interpreter only)
SRC_JS =

# Minimal runtime exports (remove all proxy functions)
LDFLAGS += -s EXPORTED_RUNTIME_METHODS="ccall,cwrap,getValue,setValue"

# Size optimization flags
LDFLAGS += -s ALLOW_MEMORY_GROWTH=1 -s EXIT_RUNTIME=0
LDFLAGS += -s SUPPORT_LONGJMP=emscripten
LDFLAGS += -s MODULARIZE -s EXPORT_NAME=_createMinimalCircuitPython

# No frozen libraries for maximum size reduction
FROZEN_MANIFEST =
```

**File: `variants/minimal-interpreter/mpconfigvariant.h` (ADDITIONS)**
```c
// Additional optimizations from Layer 1 analysis
// Memory optimization (more aggressive than Layer 1)
#define MICROPY_ALLOC_GC_STACK_SIZE (32)  // Smaller than Layer 1's 64

// Explicitly disable proxy system (from Layer 1 concept)
#define MICROPY_PROXY_C_SUPPORT (0)
#define MICROPY_JS_BRIDGE_SUPPORT (0)

// Additional size optimizations
#define MICROPY_TRACKED_ALLOC (0)
#define MICROPY_MODULE_FROZEN (0)
#define MICROPY_MODULE_FROZEN_STR (0) 
#define MICROPY_MODULE_FROZEN_MPY (0)
```

### Rationale: Why Selective Integration

1. **Size Priority**: minimal-interpreter targets <150KB vs Layer 1's 200-300KB
2. **Complexity Avoidance**: Layer 1's conditional compilation adds build complexity
3. **Purity Focus**: MicroPython flags are lighter than CircuitPython module system
4. **Build Time**: 1 hour implementation vs Layer 1's 6-9 hours

### Expected Results

- **Size**: <150KB WASM (75% better than Layer 1 target)
- **Functionality**: All core Python features (print, math, json, gc, sys)
- **Compatibility**: Standard MicroPython API
- **Build Time**: Faster compilation due to fewer features

This approach achieves **superior minimalism** while incorporating the best optimization insights from Layer 1.