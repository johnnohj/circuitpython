################################################################################
# CircuitPython WebAssembly Port Makefile
# WebAssembly port with full JavaScript-backed hardware simulation

################################################################################
# Initial setup of Makefile environment.

# Select the variant to build for:
ifdef VARIANT_DIR
# Custom variant path - remove trailing slash and get the final component of
# the path as the variant name.
VARIANT ?= $(notdir $(VARIANT_DIR:/=))
else
# If not given on the command line, then default to standard.
VARIANT ?= standard
VARIANT_DIR ?= variants/$(VARIANT)
endif

ifeq ($(wildcard $(VARIANT_DIR)/.),)
$(error Invalid VARIANT specified: $(VARIANT_DIR))
endif

# If the build directory is not given, make it reflect the variant name.
BUILD ?= build-$(VARIANT)

include ../../py/mkenv.mk
-include mpconfigport.mk
include $(VARIANT_DIR)/mpconfigvariant.mk

# Use the default frozen manifest, variants may override this.
FROZEN_MANIFEST ?= variants/manifest.py

# This should be configured by the mpconfigvariant.mk
PROG ?= circuitpython.mjs

# Qstr definitions (must come before including py.mk).
QSTR_DEFS = qstrdefsport.h
QSTR_GLOBAL_DEPENDENCIES += $(VARIANT_DIR)/mpconfigvariant.h

# Include py core make definitions.
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

# Override the version header generation to use clean version
$(HEADER_BUILD)/mpversion.h: FORCE | $(HEADER_BUILD)
	$(Q)$(PYTHON) makeversionhdr_clean.py $@

# Deduplicate source files to prevent duplicate symbol errors
SRC_C := $(sort $(SRC_C))
SRC_EXTMOD_C := $(sort $(SRC_EXTMOD_C))
SRC_SHARED_C := $(sort $(SRC_SHARED_C))
SRC_SUPERVISOR_C := $(sort $(SRC_SUPERVISOR_C))
SRC_LIB_C := $(sort $(SRC_LIB_C))
SRC_THIRDPARTY_C := $(sort $(SRC_THIRDPARTY_C))

# CIRCUITPY: Set USB configuration for WebAssembly (not used but required)
USB_NUM_ENDPOINT_PAIRS ?= 8

# CIRCUITPY: Set longint implementation (MPZ for arbitrary precision)
LONGINT_IMPL ?= MPZ

# CIRCUITPY: Override GCC version check for Emscripten
CIRCUITPY_MIN_GCC_VERSION ?= 0

# CIRCUITPY: Add essential CircuitPython identification
CFLAGS += -DCIRCUITPY=1

################################################################################
# Project specific settings and compiler/linker flags.

CC = emcc
LD = emcc
NODE ?= node
TERSER ?= npx terser

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(VARIANT_DIR)

CFLAGS += -std=c99 -Wall -Werror -Wdouble-promotion -Wfloat-conversion
CFLAGS += -Os -DNDEBUG
CFLAGS += $(INC)

# CIRCUITPY: Set CircuitPython-specific flags
CFLAGS += -DCIRCUITPY=1
CFLAGS += -DCIRCUITPY_WEBASSEMBLY=1
CFLAGS += -DEMSCRIPTEN

# Override GCC version check for Emscripten
CFLAGS += -DCIRCUITPY_MIN_GCC_VERSION=0

# Disable filesystem support for now (can be enabled later)
DISABLE_FILESYSTEM = 1
INTERNAL_FLASH_FILESYSTEM = 0
EXTERNAL_FLASH_DEVICES = ""

################################################################################
# Emscripten/WebAssembly specific flags (set by variant)

# These will be set in variants/*/mpconfigvariant.mk:
# LDFLAGS for emcc linking
# JavaScript files to concatenate

################################################################################
# Source files and libraries.

# CIRCUITPY: Use CircuitPython shared sources
SRC_SHARED = $(addprefix shared/,\
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	runtime/sys_stdio_mphal.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

# CIRCUITPY: Add supervisor stub sources
SRC_SUPERVISOR_WEBASM = supervisor/stub/filesystem.c \
	supervisor/stub/safe_mode.c \
	supervisor/stub/stack.c \
	supervisor/shared/translate/translate.c \

# Use variant-specific main.c if it exists, otherwise use default
ifeq ($(wildcard $(VARIANT_DIR)/main.c),)
MAIN_C = main.c
else
MAIN_C = $(VARIANT_DIR)/main.c
endif

# CIRCUITPY: WebAssembly-specific source files
SRC_C += \
	lexer_dedent.c \
	$(MAIN_C) \
	modjs.c \
	modjsffi.c \
	wasm_mphal.c \
	objjsproxy.c \
	proxy_c.c \
	proxy_wrapper.c \
	board.c \
	pins.c \
	fatfs_port.c \
	gccollect.c \
	webasm_stubs.c \
	common-hal/microcontroller/Pin.c \
	common-hal/digitalio/DigitalInOut.c \
	common-hal/analogio/AnalogIn.c \
	common-hal/analogio/AnalogOut.c \
	common-hal/busio/I2C.c \
	common-hal/busio/SPI.c \
	$(SRC_SUPERVISOR_WEBASM) \

# Additional extmod for unix compatibility
SRC_EXTMOD_C += extmod/modtime.c

# List of sources for qstr extraction.
SRC_QSTR += $(SRC_C) $(SRC_SHARED)

# CircuitPython WASM JS api (defined in variant)
SRC_JS ?= \
	api.js \
	objpyproxy.js \
	proxy_js.js \

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

# Deduplicate object files to prevent duplicate symbol linker errors
OBJ := $(sort $(OBJ))

# CIRCUITPY: Add translation dependency
$(BUILD)/supervisor/shared/translate/translate.o: $(HEADER_BUILD)/qstrdefs.generated.h $(HEADER_BUILD)/compressed_translations.generated.h

################################################################################
# Main targets.

.PHONY: all repl min test

all: $(BUILD)/$(PROG)

$(BUILD)/circuitpython.min.mjs: $(BUILD)/$(PROG)
	$(TERSER) $< --compress --module -o $@

repl: $(BUILD)/$(PROG)
	$(NODE) $<

# Include py core make rules.
include $(TOP)/py/mkrules.mk

# WebAssembly-specific build rule (must come after mkrules.mk to override generic rule)
$(BUILD)/$(PROG): $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)emcc $(LDFLAGS) -o $@ $(OBJ)
	$(Q)test -n "$(SRC_JS)" && cat $(SRC_JS) >> $@ || true