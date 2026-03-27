# Select the variant to build for:
ifdef VARIANT_DIR
# Custom variant path - remove trailing slash and get the final component of
# the path as the variant name.
VARIANT ?= $(notdir $(VARIANT_DIR:/=))
else
# Default variant is standard (minimal, for wasmtime CLI testing).
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

# Output binary name
PROG ?= circuitpython.wasm

# qstr definitions (must come before including py.mk)
QSTR_DEFS += qstrdefsport.h
QSTR_GLOBAL_DEPENDENCIES += $(VARIANT_DIR)/mpconfigvariant.h

# include py core make definitions
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

INC +=  -I.
INC +=  -I$(TOP)
INC += -I$(BUILD)

# ── wasi-sdk ──
WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-30.0-x86_64-linux
WASI_SYSROOT = $(WASI_SDK)/share/wasi-sysroot
WASI_CC = $(WASI_SDK)/bin/clang-21
CC  = $(WASI_CC)
LD  = $(WASI_CC)

# compiler settings
CWARN = -Wall
CWARN += -Wextra -Wno-unused-parameter -Wpointer-arith -Wdouble-promotion -Wfloat-conversion -Wno-missing-field-initializers
# wasm32 stream protocol function pointers trigger this
CWARN += -Wno-incompatible-function-pointer-types
CFLAGS += $(INC) $(CWARN) -std=gnu99 -I$(VARIANT_DIR) $(CFLAGS_EXTRA)
CFLAGS += --target=wasm32-wasi --sysroot=$(WASI_SYSROOT)
CFLAGS += -D_WASI_EMULATED_PROCESS_CLOCKS

# WASM exception handling for NLR (setjmp/longjmp)
CFLAGS += -mllvm -wasm-enable-sjlj
CFLAGS += -fwasm-exceptions

# Debugging/Optimization
ifdef DEBUG
COPT ?= -Og
else
COPT ?= -Os
COPT += -DNDEBUG
endif

CFLAGS += $(COPT)

# No stripping or size reporting for WASM binaries (host tools don't understand wasm format)
STRIP =
SIZE = true

# Linker
LDFLAGS += --target=wasm32-wasi --sysroot=$(WASI_SYSROOT)
LDFLAGS += -mllvm -wasm-enable-sjlj -fwasm-exceptions
LDFLAGS += -lsetjmp -lwasi-emulated-process-clocks
LDFLAGS += -Wl,--initial-memory=4194304
LDFLAGS += -Wl,-z,stack-size=524288
LDFLAGS += $(LDFLAGS_MOD) $(LDFLAGS_EXTRA)

# ── Optional module conditionals ──
# These are kept from the unix port for future adaptation.
# Currently all disabled for initial WASM build.

ifeq ($(MICROPY_USE_READLINE),1)
INC += -I$(TOP)/shared/readline
CFLAGS += -DMICROPY_USE_READLINE=1
SHARED_SRC_C_EXTRA += readline/readline.c
endif

# Source files — core set that compiles with wasi-sdk.
# Files kept in tree but excluded from initial build:
#   mpthreadport.c — WASM-adapted: single-threaded, yield-aware atomic sections
#   input.c — needs termios (study for event-driven input)
#   alloc.c — needs mmap (study for WASM linear memory adaptation)
#   fatfs_port.c — needs FatFS (not used, memfs instead)
#   modffi.c — needs libffi (study for JS interop adaptation)
#   modmachine.c — needs /dev/mem (study for simulated hardware)
#   modtermios.c — needs termios (study for browser terminal)
# main.c is the unix port entry point — kept for reference.
# The WASM supervisor (supervisor/) provides main() and cp_step() exports.
SRC_C += \
	supervisor/supervisor.c \
	supervisor/port.c \
	supervisor/stubs.c \
	supervisor/vm_yield.c \
	gccollect.c \
	wasi_mphal.c \
	mpthreadport.c \
	modringio.c \
	shared-module/os/__init__.c \
	supervisor/shared/translate/translate.c \
	$(SRC_MOD) \
	$(wildcard $(VARIANT_DIR)/*.c)

# CIRCUITPY-CHANGE
# CircuitPython doesn't use extmod/modtime.c, but ports/unix/extmod.c depends on it.
SRC_EXTMOD_C += extmod/modtime.c

$(BUILD)/supervisor/shared/translate/translate.o: $(HEADER_BUILD)/qstrdefs.generated.h $(HEADER_BUILD)/compressed_translations.generated.h

SHARED_SRC_C += $(addprefix shared/,\
	runtime/gchelper_generic.c \
	runtime/pyexec.c \
	timeutils/timeutils.c \
	$(SHARED_SRC_C_EXTRA) \
	)

SRC_CXX += \

OBJ = $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_CXX:.cpp=.o))
OBJ += $(addprefix $(BUILD)/, $(SHARED_SRC_C:.c=.o))

# List of sources for qstr extraction
SRC_QSTR += $(SRC_C) $(SRC_CXX) $(SHARED_SRC_C)

ifneq ($(FROZEN_MANIFEST),)
# CIRCUITPY-CHANGE
# To use frozen code create a manifest.py file with a description of files to
# freeze, then invoke make with FROZEN_MANIFEST=manifest.py (be sure to build from scratch).
CFLAGS += -DMICROPY_QSTR_EXTRA_POOL=mp_qstr_frozen_const_pool
CFLAGS += -DMICROPY_MODULE_FROZEN_MPY
CFLAGS += -DMPZ_DIG_SIZE=16 # force 16 bits to work on both 32 and 64 bit archs
CFLAGS += -DMICROPY_MODULE_FROZEN_STR
endif

CXXFLAGS += $(filter-out -Wmissing-prototypes -Wold-style-definition -std=gnu99,$(CFLAGS) $(CXXFLAGS_MOD))

include $(TOP)/py/mkrules.mk

.PHONY: test clean

test: $(BUILD)/$(PROG)
	wasmtime $(BUILD)/$(PROG)

clean:
	$(RM) -rf $(BUILD)
