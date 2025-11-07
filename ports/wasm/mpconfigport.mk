# ==============================================================================
# WASM PORT MAKEFILE CONFIGURATION
# ==============================================================================
#
# This file defines port-wide Makefile variables for the WebAssembly port.
# These settings control which modules are compiled and linked into the build.
#
# FILE HIERARCHY:
#   mpconfigport.mk         - This file: port-wide module enables (Makefile vars)
#   mpconfigport.h          - Port-wide C preprocessor defines (MICROPY_*, CIRCUITPY_*)
#   variants/*/mpconfigvariant.mk - Variant-specific Makefile overrides
#   variants/*/mpconfigvariant.h  - Variant-specific C defines
#
# NOTE ON CIRCUITPY_FULL_BUILD:
#   We don't set CIRCUITPY_FULL_BUILD=1 because that would enable ALL modules,
#   many of which require hardware implementations we haven't created yet.
#   Instead, we selectively enable modules below that work in WASM.
#
# ==============================================================================

# Enable/disable modules and 3rd-party libs to be included in interpreter

# CIRCUITPY-CHANGE: CircuitPython global marker
CFLAGS += -DCIRCUITPY=1
CIRCUITPY_FULL_BUILD = 0
# Enable VFS POSIX for Emscripten filesystem
MICROPY_VFS = 1
LONGINT_IMPL = MPZ
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


MICROPY_PY_FFI = 1

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
CIRCUITPY_USB_DEVICE = 0

# Enable async/await even though FULL_BUILD=0
MICROPY_PY_ASYNC_AWAIT = 1

# WASM has no physical LEDs
CIRCUITPY_STATUS_BAR = 0

# Enable modules that we have implemented or that work without hardware
CIRCUITPY_BOARD = 1
CIRCUITPY_BUSDEVICE = 1  # Software wrapper around busio
CIRCUITPY_ANALOGIO = 1
CIRCUITPY_BUSIO = 1
CIRCUITPY_BUSIO_SPI = 1
CIRCUITPY_BUSIO_UART = 1
CIRCUITPY_DIGITALIO = 1
CIRCUITPY_MICROCONTROLLER = 1
CIRCUITPY_NEOPIXEL_WRITE = 1
CIRCUITPY_PWMIO = 1
CIRCUITPY_ROTARYIO = 1
# CIRCUITPY_ROTARYIO_SOFTENCODER = 1
CIRCUITPY_TIME = 1

# Enable ulab for scientific computing (pure software module)
CIRCUITPY_ULAB = 1

# Pure optimization flags (no source files needed)
CIRCUITPY_BUILTINS_POW3 = 1
CIRCUITPY_OPT_MAP_LOOKUP_CACHE = 1

# Utility modules already in extmod or don't need extra sources
CIRCUITPY_BINASCII = 1
CIRCUITPY_ERRNO = 1
CIRCUITPY_JSON = 1
CIRCUITPY_OS_GETENV = 1
CIRCUITPY_RE = 1
CIRCUITPY_ZLIB = 0

# ==============================================================================
# EXPLICITLY DISABLE MODULES
# ==============================================================================
# Since CIRCUITPY_FULL_BUILD=0, many modules are already disabled by default,
# but we explicitly disable modules that might be enabled by dependencies or
# that require external libraries/hardware we don't support.

# Note: MICROPY_PY_ASYNC_AWAIT is set by circuitpy_mpconfig.mk based on CIRCUITPY_FULL_BUILD
# We enable it in mpconfigport.h instead to avoid redefinition

# Audio modules (require external libraries like mp3 decoder, or hardware)
CIRCUITPY_AUDIOBUSIO = 0
CIRCUITPY_AUDIOCORE = 0
CIRCUITPY_AUDIOIO = 0
CIRCUITPY_AUDIOMP3 = 0
CIRCUITPY_AUDIOPWMIO = 0
CIRCUITPY_AUDIOMIXER = 0
CIRCUITPY_AUDIODELAYS = 0
CIRCUITPY_AUDIOFILTERS = 0
CIRCUITPY_AUDIOFREEVERB = 0

# Display modules (need hardware or complex implementations)
CIRCUITPY_DISPLAYIO = 0
CIRCUITPY_BITMAPTOOLS = 0
CIRCUITPY_BITMAPFILTER = 0
CIRCUITPY_FRAMEBUFFERIO = 0
CIRCUITPY_PARALLELDISPLAYBUS = 0
CIRCUITPY_EPAPERDISPLAY = 0
CIRCUITPY_FOURWIRE = 0
CIRCUITPY_I2CDISPLAYBUS = 0

# Bluetooth (needs hardware)
CIRCUITPY_BLEIO = 0
CIRCUITPY_BLEIO_HCI = 0
CIRCUITPY_BLEIO_NATIVE = 0
CIRCUITPY_BLE_FILE_SERVICE = 0

# Hardware-specific modules not implemented
CIRCUITPY_ALARM = 0
CIRCUITPY_ANALOGBUFIO = 0
CIRCUITPY_BITBANGIO = 0
CIRCUITPY_BITOPS = 0
CIRCUITPY_CAMERA = 0
CIRCUITPY_CANIO = 0
CIRCUITPY_COUNTIO = 0
CIRCUITPY_DUALBANK = 0
CIRCUITPY_ESPIDF = 0
CIRCUITPY_ESPULP = 0
CIRCUITPY_FREQUENCYIO = 0
CIRCUITPY_GNSS = 0
CIRCUITPY_I2CTARGET = 0
CIRCUITPY_IMAGECAPTURE = 0
CIRCUITPY_KEYPAD = 0
CIRCUITPY_MDNS = 0
CIRCUITPY_MEMORYMAP = 0
CIRCUITPY_NEOPIXEL = 0
CIRCUITPY_NVM = 0
CIRCUITPY_ONEWIREIO = 0
CIRCUITPY_OS = 1  # Enable for filesystem operations via storage peripheral
CIRCUITPY_PS2IO = 0
CIRCUITPY_PULSEIO = 0
CIRCUITPY_RGBMATRIX = 0
CIRCUITPY_RTC = 0
CIRCUITPY_SDCARDIO = 0
CIRCUITPY_SDIOIO = 0
CIRCUITPY_SOCKETPOOL = 0
CIRCUITPY_SSL = 0
CIRCUITPY_STORAGE = 0
CIRCUITPY_SUPERVISOR = 0
CIRCUITPY_SYNTHIO = 0
CIRCUITPY_TOUCHIO = 0
CIRCUITPY_USB_CDC = 0
CIRCUITPY_USB_HID = 0
CIRCUITPY_USB_MIDI = 0
CIRCUITPY_USB_MSC = 0
CIRCUITPY_USB_VIDEO = 0
CIRCUITPY_VIDEOCORE = 0
CIRCUITPY_WATCHDOG = 0
CIRCUITPY_WIFI = 0

# Software modules not yet needed/tested
CIRCUITPY_AESIO = 0
CIRCUITPY_ATEXIT = 0
CIRCUITPY_CODEOP = 0
CIRCUITPY_GETPASS = 0
CIRCUITPY_GIFIO = 0
CIRCUITPY_HASHLIB = 0
CIRCUITPY_LOCALE = 0
CIRCUITPY_MAX3421E = 0
CIRCUITPY_MSGPACK = 0
CIRCUITPY_PIXELBUF = 0
CIRCUITPY_QRIO = 0
CIRCUITPY_TRACEBACK = 0
CIRCUITPY_USERCMODULE = 0

# Additional software modules to consider enabling later
CIRCUITPY_ARRAY = 1
CIRCUITPY_COLLECTIONS = 1
CIRCUITPY_IO = 1
CIRCUITPY_MATH = 1
CIRCUITPY_RANDOM = 1
CIRCUITPY_STRUCT = 0  # Has upstream pointer type bug (mp_uint_t* vs size_t*)
CIRCUITPY_SYS = 1
CIRCUITPY_WARNINGS = 1
