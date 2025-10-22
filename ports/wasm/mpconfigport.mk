# Enable/disable modules and 3rd-party libs to be included in interpreter

# CIRCUITPY-CHANGE: CircuitPython global marker
CFLAGS += -DCIRCUITPY=1

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
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL = 1
CIRCUITPY_ULAB = 0
MICROPY_EMIT_NATIVE = 0

# Enable VFS POSIX for Emscripten filesystem
MICROPY_VFS = 1
