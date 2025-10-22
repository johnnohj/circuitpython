# Enable/disable modules and 3rd-party libs to be included in interpreter

# This variable can take the following values:
#  0 - no readline, just simple stdin input
#  1 - use MicroPython version of readline
MICROPY_USE_READLINE = 1

# CIRCUITPY-CHANGE: not present in CircuitPython
# btree module using Berkeley DB 1.xx
MICROPY_PY_BTREE = 0

# _thread module using pthreads - not applicable for WASM
MICROPY_PY_THREAD = 0

# Subset of CPython termios module - not applicable for WASM
MICROPY_PY_TERMIOS = 0

# CIRCUITPY-CHANGE: not present in CircuitPython
# Subset of CPython socket module
MICROPY_PY_SOCKET = 0

# ffi module - disable for now, may enable later for WASM FFI
MICROPY_PY_FFI = 0

# CIRCUITPY-CHANGE: not present in CircuitPython
# ssl module requires one of the TLS libraries below
MICROPY_PY_SSL = 0
MICROPY_SSL_AXTLS = 0
MICROPY_SSL_MBEDTLS = 0

# jni module - not applicable for WASM
MICROPY_PY_JNI = 0

# Avoid using system libraries, use copies bundled with MicroPython
# as submodules (currently affects only libffi).
MICROPY_STANDALONE ?= 0

# CIRCUITPY-CHANGE: not used in CircuitPython
MICROPY_ROM_TEXT_COMPRESSION = 0

MICROPY_VFS_FAT = 1
# CIRCUITPY-CHANGE: not used in CircuitPython
MICROPY_VFS_LFS1 = 0
MICROPY_VFS_LFS2 = 0

# CIRCUITPY-CHANGE: CircuitPython-specific features
# Note: We don't set CIRCUITPY_FULL_BUILD=1 because that would enable ALL modules,
# many of which require hardware implementations we haven't created yet.
# Instead, we selectively enable modules that work in WASM or have implementations.

CIRCUITPY_MESSAGE_COMPRESSION_LEVEL = 1
MICROPY_EMIT_NATIVE = 0

# Enable modules that we have implemented or that work without hardware
CIRCUITPY_ANALOGIO = 1
CIRCUITPY_BUSIO = 1
CIRCUITPY_DIGITALIO = 1
CIRCUITPY_MICROCONTROLLER = 1
CIRCUITPY_TIME = 1

# Enable ulab for scientific computing (pure software module)
CIRCUITPY_ULAB = 1

# Enable useful utility modules that don't require hardware
# NOTE: Cannot enable aesio, atexit, busdevice, codeop, locale, msgpack, traceback
# because they require circuitpy_defns.mk which causes duplicate symbol errors

# Pure optimization flags (no source files needed)
CIRCUITPY_BUILTINS_POW3 = 1
CIRCUITPY_OPT_MAP_LOOKUP_CACHE = 1

# Utility modules already in extmod or don't need extra sources
CIRCUITPY_BINASCII = 1
CIRCUITPY_ERRNO = 1
CIRCUITPY_JSON = 1
CIRCUITPY_OS_GETENV = 1
CIRCUITPY_RE = 1
CIRCUITPY_ZLIB = 1

# Enable VFS POSIX for Emscripten filesystem
MICROPY_VFS = 1

# CIRCUITPY-CHANGE: CircuitPython global marker
CFLAGS += -DCIRCUITPY=1

# Add CFLAGS for enabled CircuitPython modules
CFLAGS += -DCIRCUITPY_ANALOGIO=$(CIRCUITPY_ANALOGIO)
CFLAGS += -DCIRCUITPY_BINASCII=$(CIRCUITPY_BINASCII)
CFLAGS += -DCIRCUITPY_BUILTINS_POW3=$(CIRCUITPY_BUILTINS_POW3)
CFLAGS += -DCIRCUITPY_BUSIO=$(CIRCUITPY_BUSIO)
CFLAGS += -DCIRCUITPY_DIGITALIO=$(CIRCUITPY_DIGITALIO)
CFLAGS += -DCIRCUITPY_ERRNO=$(CIRCUITPY_ERRNO)
CFLAGS += -DCIRCUITPY_JSON=$(CIRCUITPY_JSON)
CFLAGS += -DCIRCUITPY_MICROCONTROLLER=$(CIRCUITPY_MICROCONTROLLER)
CFLAGS += -DCIRCUITPY_OPT_MAP_LOOKUP_CACHE=$(CIRCUITPY_OPT_MAP_LOOKUP_CACHE)
CFLAGS += -DCIRCUITPY_OS_GETENV=$(CIRCUITPY_OS_GETENV)
CFLAGS += -DCIRCUITPY_RE=$(CIRCUITPY_RE)
CFLAGS += -DCIRCUITPY_TIME=$(CIRCUITPY_TIME)
CFLAGS += -DCIRCUITPY_ULAB=$(CIRCUITPY_ULAB)
CFLAGS += -DCIRCUITPY_ZLIB=$(CIRCUITPY_ZLIB)
