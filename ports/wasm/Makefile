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

include ../../py/mkenv.mk
# Include port-specific configuration
-include mpconfigport.mk
include $(VARIANT_DIR)/mpconfigvariant.mk

# Use the default frozen manifest, variants may override this.
# CIRCUITPY-CHANGE: Disable frozen modules for now (can re-enable after rotaryio works)
FROZEN_MANIFEST ?= variants/manifest.py

# Qstr definitions (must come before including py.mk).
QSTR_DEFS = qstrdefsport.h
QSTR_GLOBAL_DEPENDENCIES += $(VARIANT_DIR)/mpconfigvariant.h

include $(TOP)/py/circuitpy_mpconfig.mk
include $(TOP)/py/circuitpy_defns.mk

# include py core make definitions
include $(TOP)/py/py.mk




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
CFLAGS += -g
CFLAGS += $(INC)

EXPORTED_FUNCTIONS_EXTRA += ,\
	_board_serial_write_input,\
	_board_serial_write_input_char,\
	_board_serial_clear_input,\
	_board_serial_input_available,\
	_board_serial_set_output_callback,\
	_board_serial_repl_process_string,\
	_get_gpio_state_ptr,\
	_get_analog_state_ptr,\
	_get_pwm_state_ptr,\
	_get_neopixel_state_ptr,\
	_get_i2c_state_ptr,\
	_get_uart_state_ptr,\
	_get_spi_state_ptr,\
	_get_rotaryio_state_ptr,\
	_rotaryio_update_encoder,\
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
	_proxy_convert_mp_to_js_obj_cside,\
	_supervisor_tick_from_js

EXPORTED_RUNTIME_METHODS_EXTRA += ,\
	HEAPU8,\
	HEAPU32,\
	HEAP32,\
	PATH,\
	PATH_FS,\
	UTF8ToString,\
	getValue,\
	lengthBytesUTF8,\
	setValue,\
	stringToUTF8

JSFLAGS += -s EXPORTED_FUNCTIONS="\
	_free,\
	_main,\
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

# Library modules - JavaScript bridge for C↔JS communication
# Organized by CircuitPython module (supervisor, digitalio, analogio, etc.)
SRC_JS_LIB = $(addprefix src/library/,\
	library_supervisor.js \
	library_digitalio.js \
	library_analogio.js \
	library_pwmio.js \
	library_neopixel.js \
	library_busio.js \
	library_rotaryio.js \
)

JSFLAGS += $(addprefix --js-library ,$(SRC_JS_LIB))
JSFLAGS += -s SUPPORT_LONGJMP=emscripten
# CIRCUITPY-CHANGE: Use CircuitPython module name
JSFLAGS += -s MODULARIZE -s EXPORT_NAME=_createCircuitPythonModule

################################################################################
# Source files and libraries.

SRC_SHARED = $(addprefix shared/,\
	runtime/buffer_helper.c \
	runtime/context_manager_helpers.c \
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

# CIRCUITPY-CHANGE: Add supervisor sources for core functionality
SRC_SUPERVISOR = $(addprefix supervisor/shared/,\
	background_callback.c \
	tick.c \
	translate/translate.c \
	)

SRC_C += \
	common-hal/peripherals/pins.c \
	lexer_dedent.c \
	main.c \
	modjs.c \
	modjsffi.c \
	mphalport.c \
	objjsproxy.c \
	proxy_c.c \
	supervisor/port.c \
	supervisor/serial.c \
	supervisor/filesystem_wasm.c \
	$(SRC_MOD) \

SRC_JS += $(addprefix src/core/,\
	filesystem.js \
	api.js \
	objpyproxy.js \
	proxy_js.js \
)

# CIRCUITPY-CHANGE: Use assignment not append for first line (PY_O already includes extmod)
OBJ = $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_SUPERVISOR:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_COMMON_HAL_SHARED_MODULE_EXPANDED:.c=.o))


# List of sources for qstr extraction.
# Note: Use expanded versions with directory prefixes for file finding
SRC_QSTR += $(SRC_C) $(SRC_SHARED) $(SRC_SUPERVISOR) $(SRC_COMMON_HAL_SHARED_MODULE_EXPANDED) 

# Per-file compiler flags (to handle upstream CircuitPython code quirks)
$(BUILD)/supervisor/shared/tick.o: CFLAGS += -Wno-unused-variable

################################################################################
# Main targets.

.PHONY: all repl min test test_min

# CIRCUITPY-CHANGE: Output circuitpython.mjs with Blinka banner
all: $(BUILD)/circuitpython.mjs

# Add Blinka character (U+E000) to banner after build
$(BUILD)/circuitpython.mjs: $(OBJ) $(SRC_JS_LIB) $(SRC_JS)
	$(ECHO) "LINK $@"
	$(Q)emcc $(LDFLAGS) -o $@ $(OBJ) $(JSFLAGS)
	$(Q)cat $(SRC_JS) >> $@
# 	$(Q)cp FreeMono-Terminal-Blinka.ttf $(BUILD)/ 2>/dev/null || true

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
