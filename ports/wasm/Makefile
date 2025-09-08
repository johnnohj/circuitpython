# This file is part of the CircuitPython project: https://circuitpython.org
#
# SPDX-FileCopyrightText: Copyright (c) 2025 CircuitPython WebAssembly Contributors
#
# SPDX-License-Identifier: MIT

include ../../py/mkenv.mk

# Board name for identification
BOARD ?= CTPY_WASM

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# include py core make definitions
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

SRC_EXTMOD_C := $(sort $(SRC_EXTMOD_C))

CC = emcc
LD = emcc
CXX = em++

INC += -I.
INC += -I../..
INC += -I$(BUILD)

# Compiler settings for Emscripten
CFLAGS += $(INC) -Wall -Werror -std=gnu99 -DEMSCRIPTEN

# CircuitPython specific flags
CFLAGS += -DCIRCUITPY=1
CFLAGS += -DCIRCUITPY_WEBASSEMBLY=1

# Override GCC version check for Emscripten
CFLAGS += -DCIRCUITPY_MIN_GCC_VERSION=0

CFLAGS += -Os -DNDEBUG
CFLAGS += $(CFLAGS_EXTRA)

# Disable filesystem and translation support
DISABLE_FILESYSTEM = 1
INTERNAL_FLASH_FILESYSTEM = 0
EXTERNAL_FLASH_DEVICES = ""

# Translation settings (required even if disabled)
TRANSLATION ?= en_US
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL = 1

# CIRCUITPY: Add supervisor stub sources
SRC_SUPERVISOR_WEBASM = supervisor/stub/filesystem.c \
	supervisor/stub/safe_mode.c \
	supervisor/stub/stack.c \
	supervisor/shared/translate/translate.c \

# Set target program name
PROG ?= circuitpython.mjs

# Linker settings for WebAssembly ES module
LDFLAGS += -s WASM=1
LDFLAGS += -s EXPORTED_FUNCTIONS="_main,_mp_js_init,_mp_js_init_with_heap,_mp_js_repl_init,_mp_js_repl_process_char,_mp_hal_get_interrupt_char,_hal_register_js_provider"
LDFLAGS += -s EXPORTED_RUNTIME_METHODS="ccall,cwrap,getValue,setValue"
LDFLAGS += -s MODULARIZE -s EXPORT_NAME=_createCircuitPythonModule
LDFLAGS += -s EXPORT_ES6=1

# HAL provider system sources
SRC_HAL = \
	hal_provider.c \
	providers/stub_provider.c \
	providers/js_provider.c \

# CircuitPython compatibility modules
SRC_CIRCUITPY = \
	digitalio_module.c \

# Port-specific sources
SRC_C = \
	main.c \
	mphalport.c \
	$(SRC_HAL) \
	$(SRC_CIRCUITPY) \
	$(SRC_SUPERVISOR_WEBASM) \

SRC_QSTR += $(SRC_C)

OBJ = $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(EXTMOD_O)

# Remove duplicates from OBJ list
OBJ := $(sort $(OBJ))

# CIRCUITPY: Add translation dependency
$(BUILD)/supervisor/shared/translate/translate.o: $(HEADER_BUILD)/qstrdefs.generated.h $(HEADER_BUILD)/compressed_translations.generated.h

# Default target
all: $(BUILD)/$(PROG)

include $(TOP)/py/mkrules.mk

# Override the mkrules target to use our specific linking
$(BUILD)/$(PROG): $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)emcc $(LDFLAGS) -o $@ $(sort $(OBJ)) $(LIBS)

# Specific source handling
$(BUILD)/shared-bindings/%: | $(HEADER_BUILD)/qstr.i.last
$(BUILD)/shared-module/%: | $(HEADER_BUILD)/qstr.i.last

print-cfg:
	$(info HAL sources: $(SRC_HAL))
	$(info CircuitPython sources: $(SRC_CIRCUITPY))
	$(info Port sources: $(SRC_C))
