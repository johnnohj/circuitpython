# CircuitPython WebAssembly Bare Variant
# Minimal CircuitPython interpreter with no hardware dependencies
# Focus on core Python functionality only

# Disable all hardware modules
CIRCUITPY_DIGITALIO = 0
CIRCUITPY_ANALOGIO = 0
CIRCUITPY_BUSIO = 0
CIRCUITPY_MICROCONTROLLER = 0
CIRCUITPY_BOARD = 0
CIRCUITPY_SUPERVISOR = 0
CIRCUITPY_USB_CDC = 0
CIRCUITPY_USB_HID = 0
CIRCUITPY_USB_MIDI = 0
CIRCUITPY_USB_MSC = 0
CIRCUITPY_STORAGE = 0

# Disable complex filesystem operations
INTERNAL_FLASH_FILESYSTEM = 0
QSPI_FLASH_FILESYSTEM = 0
SPI_FLASH_FILESYSTEM = 0

# Disable internal libc to prevent duplicate symbols
CIRCUITPY_USE_INTERNAL_LIBC = 0

# Override to remove duplicate libc sources
SRC_EXTMOD_C := $(filter-out shared/libc/abort_.c shared/libc/printf.c, $(SRC_EXTMOD_C))
SRC_CIRCUITPY_COMMON := $(filter-out shared/libc/string0.c, $(SRC_CIRCUITPY_COMMON))

# Enable only essential core modules (disable asyncio module to avoid duplicates)
CIRCUITPY_SYS = 1
CIRCUITPY_TIME = 1
CIRCUITPY_MATH = 1
CIRCUITPY_RANDOM = 1
CIRCUITPY_OS = 1
CIRCUITPY_ASYNCIO = 0

# Use minimal frozen manifest
FROZEN_MANIFEST ?= $(VARIANT_DIR)/manifest.py

# WebAssembly specific settings for bare interpreter
JSFLAGS += -s INITIAL_MEMORY=1048576  # 1MB initial memory (minimal)
JSFLAGS += -s ALLOW_MEMORY_GROWTH=1   # Allow growth as needed
JSFLAGS += -s MODULARIZE=1            # Generate modular output
JSFLAGS += -s EXPORT_NAME="'CircuitPython'" # Export name
JSFLAGS += -s ASYNCIFY                # Enable async/await support for JavaScript integration