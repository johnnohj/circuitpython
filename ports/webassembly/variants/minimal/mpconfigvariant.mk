# Build a minimal CircuitPython WebAssembly interpreter
# Based on successful unix minimal variant

FROZEN_MANIFEST = variants/manifest.py

# Disable modules that caused issues in unix minimal build
MICROPY_PY_BTREE = 0
MICROPY_PY_FFI = 0
MICROPY_PY_SOCKET = 0
MICROPY_PY_THREAD = 0
MICROPY_PY_TERMIOS = 1
MICROPY_PY_SSL = 0
MICROPY_USE_READLINE = 1

# Disable filesystem modules for minimal build
MICROPY_VFS = 0
MICROPY_VFS_FAT = 0
MICROPY_VFS_LFS1 = 0
MICROPY_VFS_LFS2 = 0

# Disable CircuitPython modules that require float support or are heavyweight
CIRCUITPY_ULAB = 0

# Enable essential core modules (following bare variant approach)
CIRCUITPY_SYS = 1
CIRCUITPY_TIME = 1
CIRCUITPY_MATH = 1
CIRCUITPY_RANDOM = 1
CIRCUITPY_OS = 1
CIRCUITPY_ASYNCIO = 0



# CRITICAL: Remove duplicate libc sources to prevent symbol conflicts with Emscripten
# Emscripten provides its own libc implementations of these functions:
# - abort_.c provides abort_() which conflicts with Emscripten's abort()
# - printf.c provides vsnprintf() which conflicts with Emscripten's vsnprintf()
# - string0.c provides string functions which conflict with Emscripten's libc
# This filtering approach is proven to work (used in bare variant)
SRC_EXTMOD_C := $(filter-out shared/libc/abort_.c shared/libc/printf.c, $(SRC_EXTMOD_C))
SRC_CIRCUITPY_COMMON := $(filter-out shared/libc/string0.c, $(SRC_CIRCUITPY_COMMON))

# WebAssembly-specific: Keep JavaScript interop minimal but functional
JSFLAGS += -s ASYNCIFY
JSFLAGS += -s INITIAL_MEMORY=1048576
JSFLAGS += -s ALLOW_MEMORY_GROWTH=1

# Linker flags to handle duplicate symbols gracefully
# Emscripten/wasm-ld flags to allow multiple definitions (first one wins)
LDFLAGS += -Wl,--allow-multiple-definition

# Additional Emscripten-specific options for handling duplicates
JSFLAGS += -s ALLOW_UNIMPLEMENTED_SYSCALLS=1
JSFLAGS += -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE=[]

CFLAGS += -DMICROPY_REPL_EVENT_DRIVEN=1

# Alternative options if the above doesn't work (uncomment as needed):
# LDFLAGS += -Wl,--warn-unresolved-symbols      # Warn but don't fail on unresolved symbols
# LDFLAGS += -Wl,--unresolved-symbols=ignore-all # Ignore unresolved symbols completely
# JSFLAGS += -s ERROR_ON_UNDEFINED_SYMBOLS=0     # Don't error on undefined symbols
