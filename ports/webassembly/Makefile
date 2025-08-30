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
include mpconfigport.mk
include $(VARIANT_DIR)/mpconfigvariant.mk

# Use the default frozen manifest, variants may override this.
FROZEN_MANIFEST ?= variants/manifest.py

# Qstr definitions (must come before including py.mk).
QSTR_DEFS = qstrdefsport.h

# Include py core make definitions.
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk
# Add WebAssembly-specific extmod additions
include extmod-wasm.mk

# Remove problematic libc sources for bare variant to prevent duplicates
ifeq ($(VARIANT),bare)
SRC_EXTMOD_C := $(filter-out shared/libc/abort_.c shared/libc/printf.c, $(SRC_EXTMOD_C))
endif

# Deduplicate source files to prevent duplicate symbol errors
# This handles cases where sources are included from multiple makefiles
SRC_C := $(sort $(SRC_C))
SRC_EXTMOD_C := $(sort $(SRC_EXTMOD_C))
SRC_SHARED_C := $(sort $(SRC_SHARED_C))
SRC_SUPERVISOR_C := $(sort $(SRC_SUPERVISOR_C))
SRC_LIB_C := $(sort $(SRC_LIB_C))
SRC_THIRDPARTY_C := $(sort $(SRC_THIRDPARTY_C))

# CIRCUITPY: Following unix port approach - no automatic feature generation

# CIRCUITPY: Set USB configuration for WebAssembly (not used but required)
USB_NUM_ENDPOINT_PAIRS ?= 8

# CIRCUITPY: Set longint implementation (MPZ for arbitrary precision)
LONGINT_IMPL ?= MPZ

# CIRCUITPY: Override GCC version check for Emscripten
# Emscripten reports __GNUC__ = 4 but is actually based on modern Clang
CIRCUITPY_MIN_GCC_VERSION ?= 0

# CIRCUITPY: Unix port approach - no automatic feature configuration
# Only enable essential CircuitPython identification
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

# Override GCC version check for Emscripten
# Emscripten is based on modern Clang but reports __GNUC__ = 4
CFLAGS += -DCIRCUITPY_MIN_GCC_VERSION=0

# Disable filesystem support for now (can be enabled later)
DISABLE_FILESYSTEM = 1
INTERNAL_FLASH_FILESYSTEM = 0
EXTERNAL_FLASH_DEVICES = ""

EXPORTED_FUNCTIONS_EXTRA += ,\
	_mp_js_do_exec,\
	_mp_js_do_exec_async,\
	_mp_js_do_import,\
	_mp_js_register_js_module,\
	_proxy_c_free_obj,\
	_proxy_c_init,\
	_proxy_c_to_js_call,\
	_proxy_c_to_js_delete_attr,\
	_proxy_c_to_js_dir,\
	_proxy_c_to_js_get_array,\
	_proxy_c_to_js_get_dict,\
	_proxy_c_to_js_get_iter,\
	_proxy_c_to_js_get_type,\
	_proxy_c_to_js_has_attr,\
	_proxy_c_to_js_iternext,\
	_proxy_c_to_js_lookup_attr,\
	_proxy_c_to_js_resume,\
	_proxy_c_to_js_store_attr,\
	_proxy_convert_mp_to_js_obj_cside

EXPORTED_RUNTIME_METHODS_EXTRA += ,\
	PATH,\
	PATH_FS,\
	UTF8ToString,\
	getValue,\
	lengthBytesUTF8,\
	setValue,\
	stringToUTF8

JSFLAGS += -s EXPORTED_FUNCTIONS="\
	_free,\
	_malloc,\
	_mp_js_init,\
	_mp_js_repl_init,\
	_mp_js_repl_process_char,\
	_mp_hal_get_interrupt_char,\
	_mp_sched_keyboard_interrupt$(EXPORTED_FUNCTIONS_EXTRA)"
JSFLAGS += -s EXPORTED_RUNTIME_METHODS="\
	ccall,\
	cwrap,\
	FS$(EXPORTED_RUNTIME_METHODS_EXTRA)"
JSFLAGS += --js-library library.js
JSFLAGS += -s SUPPORT_LONGJMP=emscripten
JSFLAGS += -s MODULARIZE -s EXPORT_NAME=_createCircuitPythonModule

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

# CIRCUITPY: Add supervisor stub sources (following unix port approach)
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

SRC_C += \
	lexer_dedent.c \
	$(MAIN_C) \
	modjs.c \
	modjsffi.c \
	mphalport.c \
	objjsproxy.c \
	proxy_c.c \
	webassembly_stubs.c \
	board.c \
	fatfs_port.c \
	$(SRC_SUPERVISOR_WEBASM) \

# List of sources for qstr extraction.
SRC_QSTR += $(SRC_C) $(SRC_SHARED)

# CircuitPython WASM JS api
SRC_JS += \
	coreApi.js \
	nodeApi.js \
	workerApi.js \
	objpyproxy.js \
	proxy_js.js \

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

# Deduplicate object files to prevent duplicate symbol linker errors
OBJ := $(sort $(OBJ))

# CIRCUITPY: Add translation dependency
$(BUILD)/supervisor/shared/translate/translate.o: $(HEADER_BUILD)/qstrdefs.generated.h $(HEADER_BUILD)/compressed_translations.generated.h

# CIRCUITPY: Use WebAssembly-specific devices.h stub instead of generating from nvm.toml
$(HEADER_BUILD)/devices.h: devices_stub.h | $(HEADER_BUILD)
	$(ECHO) "GEN $@"
	$(Q)cp $< $@

################################################################################
# Main targets.

.PHONY: all repl min test test_min

min: $(BUILD)/circuitpython.min.mjs

all: $(BUILD)/circuitpython.mjs

$(BUILD)/circuitpython.mjs: $(OBJ) library.js
	$(ECHO) "LINK $@"
	$(Q)emcc $(LDFLAGS) -o $@ $(OBJ) $(JSFLAGS)
	$(Q)test -n "$(SRC_JS)" && cat $(SRC_JS) >> $@ || true

# Keep the micropython.mjs target for backward compatibility
$(BUILD)/micropython.mjs: $(BUILD)/circuitpython.mjs
	$(Q)cp $< $@

$(BUILD)/circuitpython.min.mjs: $(BUILD)/circuitpython.mjs
	$(TERSER) $< --compress --module -o $@

# Keep the micropython.min.mjs target for backward compatibility
$(BUILD)/micropython.min.mjs: $(BUILD)/circuitpython.min.mjs
	$(Q)cp $< $@

repl: $(BUILD)/circuitpython.mjs
	$(NODE) $<

min: $(BUILD)/micropython.min.mjs

test: $(BUILD)/circuitpython.mjs $(TOP)/tests/run-tests.py
	cd $(TOP)/tests && MICROPY_MICROPYTHON_MJS=../ports/webassembly/$< ./run-tests.py -t webassembly

test_min: $(BUILD)/circuitpython.min.mjs $(TOP)/tests/run-tests.py
	cd $(TOP)/tests && MICROPY_MICROPYTHON_MJS=../ports/webassembly/$< ./run-tests.py -t webassembly

################################################################################
# Remaining make rules.

include $(TOP)/py/mkrules.mk
