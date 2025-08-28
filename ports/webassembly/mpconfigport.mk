# Enable/disable modules and 3rd-party libs to be included in interpreter
# Configuration for CircuitPython WebAssembly port

# Build for WebAssembly
MICROPY_FORCE_32BIT = 0

# Use MicroPython version of readline
MICROPY_USE_READLINE = 1

# Disable modules not suitable for WebAssembly
MICROPY_PY_BTREE = 0
MICROPY_PY_THREAD = 1
MICROPY_PY_TERMIOS = 1
MICROPY_PY_SOCKET = 0
MICROPY_PY_FFI = 1
MICROPY_PY_SSL = 0
MICROPY_SSL_AXTLS = 0
MICROPY_SSL_MBEDTLS = 0
MICROPY_PY_JNI = 0

# Use bundled libraries
MICROPY_STANDALONE = 1

# Disable text compression for simplicity
MICROPY_ROM_TEXT_COMPRESSION = 0

# Filesystem support (disabled for now)
MICROPY_VFS_FAT = 1
MICROPY_VFS_LFS1 = 0
MICROPY_VFS_LFS2 = 0

# CircuitPython-specific settings for WebAssembly
CIRCUITPY_ULAB = 0
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL = 1
MICROPY_EMIT_NATIVE = 0

# CircuitPython flag
CFLAGS += -DCIRCUITPY=1

# Enable basic I/O and system functionality
MICROPY_PY_SYS = 1
MICROPY_PY_TIME = 1
MICROPY_PY_MATH = 1
MICROPY_PY_RANDOM = 1

# Enable JavaScript integration
MICROPY_PY_JS = 1
MICROPY_PY_JSFFI = 1
