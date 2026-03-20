################################################################################
# WASI port Makefile
#
# Headless CircuitPython VM compiled with wasi-sdk.
# No Emscripten, no JavaScript. All I/O through WASI fd_read/fd_write.
#
# Variants:
#   standard — simple REPL on stdin/stdout (plain MicroPython)
#   opfs     — CIRCUITPY=1, supervisor infrastructure, OPFS-backed state
#
# Usage:
#   make                          # build standard variant
#   make VARIANT=opfs             # build OPFS executor variant
################################################################################

VARIANT ?= standard
VARIANT_DIR ?= variants/$(VARIANT)
BUILD ?= build-$(VARIANT)

# ── wasi-sdk paths ──
WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-30.0-x86_64-linux
WASI_SYSROOT = $(WASI_SDK)/share/wasi-sysroot
WASI_CC = $(WASI_SDK)/bin/clang-21

include ../../py/mkenv.mk
-include $(VARIANT_DIR)/mpconfigvariant.mk

# Qstr definitions (must come before py.mk)
QSTR_DEFS += qstrdefsport.h

# Include py core make definitions
include $(TOP)/py/py.mk

################################################################################
# Compiler / linker

CC  = $(WASI_CC)
LD  = $(WASI_CC)

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(VARIANT_DIR)

CFLAGS += --target=wasm32-wasi
CFLAGS += --sysroot=$(WASI_SYSROOT)
CFLAGS += -std=c99 -Wall -Wdouble-promotion -Wfloat-conversion
CFLAGS += -Os -DNDEBUG
CFLAGS += -D_POSIX_C_SOURCE=199309L
CFLAGS += $(INC)

# setjmp/longjmp via WASM Exception Handling (required for NLR)
CFLAGS += -mllvm -wasm-enable-sjlj

# CircuitPython mode (same as unix port): enables CP code paths in py/*.c
CFLAGS += -DCIRCUITPY=1
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL ?= 0
USB_NUM_ENDPOINT_PAIRS ?= 0
CIRCUITPY_USB_DEVICE ?= 0

LDFLAGS += --target=wasm32-wasi
LDFLAGS += --sysroot=$(WASI_SYSROOT)
LDFLAGS += -mllvm -wasm-enable-sjlj
LDFLAGS += -lsetjmp
LDFLAGS += -Wl,--initial-memory=4194304

################################################################################
# Source files

# Common HAL (used by all variants)
SRC_C_COMMON = \
	wasi_mphal.c \

# CircuitPython supervisor stubs + translate (used by all variants with CIRCUITPY=1)
SRC_SUPERVISOR = \
	supervisor/shared/translate/translate.c \
	supervisor/stub/filesystem.c \
	supervisor/stub/safe_mode.c \
	supervisor/stub/stack.c \

# ---- Variant-specific sources ----

ifeq ($(VARIANT),opfs)

# OPFS variant: executor with state checkpointing + VM hooks
SRC_C = \
	$(SRC_C_COMMON) \
	main_opfs.c \
	opfs_state.c \
	dev_repl.c \

SRC_SHARED_EXTRA = \
	$(SRC_SUPERVISOR) \

else

# Standard variant: simple REPL
SRC_C = \
	$(SRC_C_COMMON) \
	main.c \
	gccollect.c \

SRC_SHARED_EXTRA = \
	$(SRC_SUPERVISOR) \

endif

SRC_SHARED = $(addprefix shared/,\
	runtime/gchelper_generic.c \
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

# Add supervisor sources (paths are relative to TOP)
SRC_SHARED += $(SRC_SHARED_EXTRA)

SRC_QSTR += $(SRC_C) $(SRC_SHARED)

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

################################################################################
# Build targets

.PHONY: all clean test

all: $(BUILD)/micropython.wasm

$(BUILD)/micropython.wasm: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJ)

test: $(BUILD)/micropython.wasm
	wasmtime $<

clean:
	$(RM) -rf $(BUILD)

################################################################################
include $(TOP)/py/mkrules.mk
