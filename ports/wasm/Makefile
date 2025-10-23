################################################################################
# Initial setup of Makefile environment.

# CIRCUITPY-CHANGE: Use CircuitPython paths
# This Makefile should work from the CircuitPython tree

# Select the variant to build for:
ifdef VARIANT_DIR
# Custom variant path - remove trailing slash and get the final component of
# the path as the variant name.
VARIANT ?= $(notdir $(VARIANT_DIR:/=))
else
# CIRCUITPY-CHANGE: Default to 'standard' variant
VARIANT ?= standard
VARIANT_DIR ?= variants/$(VARIANT)
endif

ifeq ($(wildcard $(VARIANT_DIR)/.),)
$(error Invalid VARIANT specified: $(VARIANT_DIR))
endif

# If the build directory is not given, make it reflect the variant name.
BUILD ?= build-$(VARIANT)

# CIRCUITPY-CHANGE: Include CircuitPython build system
include ../../py/mkenv.mk
include $(VARIANT_DIR)/mpconfigvariant.mk

# Use the default frozen manifest, variants may override this.
FROZEN_MANIFEST ?= variants/manifest.py

# Qstr definitions (must come before including py.mk).
QSTR_DEFS = qstrdefsport.h

# Include port-specific configuration
include mpconfigport.mk

# CIRCUITPY-CHANGE: Include CircuitPython py core make definitions
# Note: py.mk already includes extmod/extmod.mk, so we don't include it again
include $(TOP)/py/py.mk

# NOTE: We cannot include $(TOP)/py/circuitpy_defns.mk because it causes
# duplicate symbol errors (uz lib, etc.). For now, only enable modules that
# don't require additional source files or that are already in extmod.

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

CFLAGS += -std=c99 -Wall -Werror -Wno-double-promotion -Wno-float-conversion
CFLAGS += -Os -DNDEBUG
CFLAGS += $(INC)

EXPORTED_FUNCTIONS_EXTRA += ,\
	_board_serial_write_input,\
	_board_serial_write_input_char,\
	_board_serial_clear_input,\
	_board_serial_input_available,\
	_board_serial_set_output_callback,\
	_board_serial_repl_process_string,\
	_get_virtual_hardware_ptr,\
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
	HEAPU8,\
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
# CIRCUITPY-CHANGE: Use CircuitPython module name
JSFLAGS += -s MODULARIZE -s EXPORT_NAME=_createCircuitPythonModule

################################################################################
# Source files and libraries.

SRC_SHARED = $(addprefix shared/,\
	runtime/context_manager_helpers.c \
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

# CIRCUITPY-CHANGE: Add supervisor sources for translation/decompression
SRC_SUPERVISOR = $(addprefix supervisor/shared/,\
	translate/translate.c \
	)
SRC_SUPERVISOR += supervisor/port.c

# CIRCUITPY-CHANGE: Manually add CircuitPython module sources
SRC_BINDINGS = $(addprefix shared-bindings/,\
	analogio/AnalogIn.c \
	analogio/AnalogOut.c \
	analogio/__init__.c \
	board/__init__.c \
	busio/I2C.c \
	busio/SPI.c \
	busio/UART.c \
	busio/__init__.c \
	digitalio/DigitalInOut.c \
	digitalio/Direction.c \
	digitalio/DriveMode.c \
	digitalio/Pull.c \
	digitalio/__init__.c \
	microcontroller/Pin.c \
	microcontroller/Processor.c \
	microcontroller/ResetReason.c \
	microcontroller/RunMode.c \
	microcontroller/__init__.c \
	time/__init__.c \
	util.c \
	)

SRC_COMMON_HAL = $(addprefix common-hal/,\
	analogio/AnalogIn.c \
	analogio/AnalogOut.c \
	analogio/__init__.c \
	board/__init__.c \
	busio/I2C.c \
	busio/SPI.c \
	busio/UART.c \
	busio/__init__.c \
	digitalio/DigitalInOut.c \
	digitalio/__init__.c \
	microcontroller/Pin.c \
	microcontroller/Processor.c \
	microcontroller/__init__.c \
	time/__init__.c \
	)

SRC_C += \
	background.c \
	board_serial.c \
	lexer_dedent.c \
	main.c \
	message_queue.c \
	modjs.c \
	modjsffi.c \
	mphalport.c \
	objjsproxy.c \
	pins.c \
	proxy_c.c \
	shared_memory.c \

# List of sources for qstr extraction.
SRC_QSTR += $(SRC_C) $(SRC_SHARED) $(SRC_SUPERVISOR) $(SRC_BINDINGS) $(SRC_COMMON_HAL)

SRC_JS += \
	virtual_clock.js \
	api.js \
	objpyproxy.js \
	proxy_js.js \

# CIRCUITPY-CHANGE: Use assignment not append for first line (PY_O already includes extmod)
OBJ = $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_SUPERVISOR:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_BINDINGS:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_COMMON_HAL:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_MOD:.c=.o))

################################################################################
# Main targets.

.PHONY: all repl min test test_min

# CIRCUITPY-CHANGE: Output circuitpython.mjs with Blinka banner
all: $(BUILD)/circuitpython.mjs

# Add Blinka character (U+E000) to banner after build
$(BUILD)/circuitpython.mjs: $(OBJ) library.js $(SRC_JS)
	@# Add Blinka character (U+E000) to generated mpversion.h if it doesn't have it already
	@# U+E000 in UTF-8 is: 0xEE 0x80 0x80
	@if [ -f $(BUILD)/genhdr/mpversion.h ] && ! grep -q '\\xEE\\x80\\x80' $(BUILD)/genhdr/mpversion.h; then \
		sed -i.bak 's/"Adafruit CircuitPython/"\\xEE\\x80\\x80 Adafruit CircuitPython/' $(BUILD)/genhdr/mpversion.h && \
		rm -f $(BUILD)/genhdr/mpversion.h.bak; \
	fi
	$(ECHO) "LINK $@"
	$(Q)emcc $(LDFLAGS) -o $@ $(OBJ) $(JSFLAGS)
	$(Q)cat $(SRC_JS) >> $@
	$(Q)cp FreeMono-Terminal-Blinka.ttf $(BUILD)/ 2>/dev/null || true

$(BUILD)/circuitpython.min.mjs: $(BUILD)/circuitpython.mjs
	$(TERSER) $< --compress --module -o $@

repl: $(BUILD)/circuitpython.mjs
	$(NODE) $<

min: $(BUILD)/circuitpython.min.mjs

test: $(BUILD)/circuitpython.mjs
	@echo "CircuitPython WASM test target - tests not yet configured"

test_min: $(BUILD)/circuitpython.min.mjs
	@echo "CircuitPython WASM test target - tests not yet configured"

# Note: 'clean' target is provided by mkrules.mk

################################################################################
# Remaining make rules.

include $(TOP)/py/mkrules.mk
