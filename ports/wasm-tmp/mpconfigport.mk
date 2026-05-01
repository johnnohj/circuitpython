# Enable/disable modules and 3rd-party libs to be included in interpreter
# WASM port — based on unix port defaults, adapted for wasi-sdk

# Readline (uses shared/readline, no termios dependency)
MICROPY_USE_READLINE = 1

# Threading — disabled for WASM (single-threaded)
MICROPY_PY_THREAD = 0

# Terminal I/O — disabled (no termios in WASI)
MICROPY_PY_TERMIOS = 0

# FFI — disabled (no dynamic library loading)
MICROPY_PY_FFI = 0

# Socket, SSL, BTree, JNI — not applicable
MICROPY_PY_SOCKET = 0
MICROPY_PY_SSL = 0
MICROPY_PY_BTREE = 0
MICROPY_PY_JNI = 0

# Filesystem — POSIX VFS via WASI, no FatFS/LFS
MICROPY_VFS_FAT = 0
MICROPY_VFS_LFS1 = 0
MICROPY_VFS_LFS2 = 0

# Native emitters — not applicable (bytecode interpreter IS wasm)
MICROPY_EMIT_NATIVE = 0

# CircuitPython mode
CFLAGS += -DCIRCUITPY=1
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL = 0
CIRCUITPY_ULAB = 0
