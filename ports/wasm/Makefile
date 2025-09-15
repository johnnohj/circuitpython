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

# Add WebAssembly-specific extmod additions (learned from trial2WASM)
# include extmod-wasm.mk

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

# Add missing shared runtime sources
SRC_SHARED_MODULE = \
	shared/runtime/pyexec.c \
	shared/runtime/interrupt_char.c \
	shared/runtime/stdout_helpers.c \
	shared/readline/readline.c \

# Minimal CircuitPython shared-bindings (disabled to isolate table bounds error)
SRC_SHARED_BINDINGS = \

# Minimal CircuitPython shared-module (disabled for now)  
SRC_SHARED_MODULE_CP = \

# Set target program name
PROG ?= circuitpython.mjs

# Linker settings for WebAssembly ES module
LDFLAGS += -s WASM=1
LDFLAGS += -s EXPORTED_FUNCTIONS="_main,_free,_malloc,_mp_js_init,_mp_js_init_with_heap,_mp_js_deinit,_mp_js_repl_init,_mp_js_repl_process_char,_mp_hal_get_interrupt_char,_hal_register_js_provider,_mp_js_get_blinka_glyph_path,_mp_js_get_blinka_char,_mp_js_init_generic_board,_mp_js_get_generic_board_json,_mp_js_generic_pin_set_value,_mp_js_generic_pin_get_value,_mp_js_generic_pin_set_direction,_mp_js_generate_board_module"
LDFLAGS += -s EXPORTED_RUNTIME_METHODS="ccall,cwrap,getValue,setValue,FS,HEAPU8,stringToUTF8,lengthBytesUTF8,UTF8ToString,allocateUTF8"
LDFLAGS += -s MODULARIZE -s EXPORT_NAME=_createCircuitPythonModule
LDFLAGS += -s EXPORT_ES6=1
# Enable filesystem support for stdout/stderr
LDFLAGS += -s FORCE_FILESYSTEM=1
LDFLAGS += -s EXIT_RUNTIME=0
# Allow memory growth for Python heap
LDFLAGS += -s ALLOW_MEMORY_GROWTH=1
# Support longjmp (needed for Python exceptions)
LDFLAGS += -s SUPPORT_LONGJMP=emscripten
# Let Emscripten automatically manage WebAssembly table (like MicroPython does)
# Remove explicit table size constraints that may be causing bounds issues

# HAL provider system sources
SRC_HAL = \
	hal_provider.c \
	generic_board.c \
	providers/stub_provider.c \
	providers/js_provider.c \

# CircuitPython compatibility modules
SRC_CIRCUITPY = \
	digitalio_module.c \
	dynamic_modules.c \

# Port-specific sources
SRC_C = \
	main.c \
	mphalport.c \
	webasm_stubs.c \
	$(SRC_HAL) \
	$(SRC_CIRCUITPY) \
	$(SRC_SUPERVISOR_WEBASM) \
	$(SRC_SHARED_MODULE) \
	$(SRC_SHARED_BINDINGS) \
	$(SRC_SHARED_MODULE_CP) \

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

# Convenience targets for dual builds
.PHONY: browser node both clean-browser clean-node size-compare

browser:
	@echo "Building CircuitPython for Browser (minimal/optimized)..."
	@mkdir -p build-browser
	$(MAKE) clean
	$(MAKE) LDFLAGS="$(LDFLAGS) -s ENVIRONMENT=web -s INITIAL_MEMORY=2MB"
	@cp build/circuitpython.mjs build-browser/circuitpython-web.mjs
	@cp build/circuitpython.wasm build-browser/circuitpython-web.wasm
	@echo "Browser build complete: build-browser/circuitpython-web.mjs + circuitpython-web.wasm"
	@echo "Size: $$(wc -c < build-browser/circuitpython-web.mjs) bytes (MJS) + $$(wc -c < build-browser/circuitpython-web.wasm) bytes (WASM)"

node:
	@echo "Building CircuitPython for Node.js (full features)..."
	@mkdir -p build-node
	$(MAKE) clean
	$(MAKE) VARIANT=node \
		CFLAGS_EXTRA="-Wno-macro-redefined -DMICROPY_CONFIG_ROM_LEVEL=MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES -DMICROPY_PY_JSON=1 -DMICROPY_PY_RE=1 -DMICROPY_PY_HASHLIB=1 -DMICROPY_PY_BINASCII=1 -DMICROPY_PY_RANDOM=1 -DMICROPY_PY_ERRNO=1 -DMICROPY_PY_SELECT=1 -DMICROPY_PY_ASYNCIO=1 -DMICROPY_PY_ZLIB=1 -DMICROPY_PY_DEFLATE=1 -DMICROPY_PY_SYS_PLATFORM=\\\"nodejs\\\" -DMICROPY_PY_SYS_EXIT=1" \
		LDFLAGS="$(LDFLAGS) -s ENVIRONMENT=node -s NODERAWFS=1 -s INITIAL_MEMORY=8MB -s MAXIMUM_MEMORY=256MB -O2"
	@if [ -f build/circuitpython.mjs ]; then \
		cp build/circuitpython.mjs build-node/circuitpython-node.mjs; \
		cp build/circuitpython.wasm build-node/circuitpython-node.wasm; \
		echo "Node.js build complete: build-node/circuitpython-node.mjs + circuitpython-node.wasm"; \
	fi

both: browser node

size-compare: both
	@echo "=== Build Size Comparison ==="
	@if [ -f build-browser/circuitpython-web.mjs ] && [ -f build-node/circuitpython-node.mjs ]; then \
		browser_size=$$(wc -c < build-browser/circuitpython-web.mjs); \
		node_size=$$(wc -c < build-node/circuitpython-node.mjs); \
		echo "Browser: $$browser_size bytes ($$(du -h build-browser/circuitpython-web.mjs | cut -f1))"; \
		echo "Node.js: $$node_size bytes ($$(du -h build-node/circuitpython-node.mjs | cut -f1))"; \
	fi

clean-browser:
	rm -rf build-browser

clean-node:
	rm -rf build-node

print-cfg:
	$(info VARIANT: $(VARIANT))
	$(info PROG: $(PROG))
	$(info BUILD: $(BUILD))
	$(info HAL sources: $(SRC_HAL))
	$(info CircuitPython sources: $(SRC_CIRCUITPY))
	$(info Port sources: $(SRC_C))
