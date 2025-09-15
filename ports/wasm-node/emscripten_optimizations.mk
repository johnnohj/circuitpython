# Emscripten optimization recommendations for CircuitPython WebAssembly port

# Memory Management Optimizations
JSFLAGS += -s ALLOW_MEMORY_GROWTH=1              # Dynamic memory growth
JSFLAGS += -s INITIAL_MEMORY=8MB                 # Start with reasonable size
JSFLAGS += -s MAXIMUM_MEMORY=256MB               # Cap maximum memory
JSFLAGS += -s STACK_SIZE=128KB                   # Larger stack for Python recursion

# WebAssembly Features
JSFLAGS += -s WASM_BIGINT=1                      # Better integer handling
JSFLAGS += -s USE_PTHREADS=0                     # Disable threads for now
JSFLAGS += -s ASYNCIFY=1                         # Enable async/await support
JSFLAGS += -s ASYNCIFY_STACK_SIZE=16384          # Async stack size

# Optimization Flags
JSFLAGS += -O3                                   # Maximum optimization
JSFLAGS += -flto                                 # Link-time optimization
JSFLAGS += --closure 1                           # JavaScript minification
JSFLAGS += -s ENVIRONMENT=web,webview,worker,node # Multi-environment support

# Module System
JSFLAGS += -s MODULARIZE=1                       # ES6 module output
JSFLAGS += -s EXPORT_ES6=1                       # Modern module syntax
JSFLAGS += -s USE_ES6_IMPORT_META=1              # ES6 import.meta support
JSFLAGS += -s SINGLE_FILE=0                      # Separate .wasm file

# Debugging Support (disable in production)
ifdef DEBUG
JSFLAGS += -s ASSERTIONS=2                       # Runtime assertions
JSFLAGS += -s SAFE_HEAP=1                        # Memory safety checks
JSFLAGS += -s STACK_OVERFLOW_CHECK=2             # Stack overflow detection
JSFLAGS += -g                                    # Debug symbols
JSFLAGS += --source-map-base http://localhost:8000/ # Source maps
endif

# File System Configuration
JSFLAGS += -s FORCE_FILESYSTEM=0                 # Lazy load filesystem
JSFLAGS += -s NODERAWFS=0                        # Disable Node raw FS

# WASI Support (experimental)
JSFLAGS += -s STANDALONE_WASM=0                  # Not standalone yet
JSFLAGS += -s PROXY_TO_PTHREAD=0                 # No pthread proxying

# Additional Exports for CircuitPython
EXPORTED_FUNCTIONS_EXTRA += ,\
	_mp_js_register_board_config,\
	_mp_js_stdin_write_char,\
	_mp_js_stdin_write_str,\
	_mp_hal_is_stdin_raw_mode

EXPORTED_RUNTIME_METHODS_EXTRA += ,\
	HEAP8,HEAP16,HEAP32,HEAPU8,HEAPU16,HEAPU32,\
	allocate,allocateUTF8,allocateUTF8OnStack