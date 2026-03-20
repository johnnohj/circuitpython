################################################################################
# WASI port Makefile
#
# Headless CircuitPython/MicroPython VM compiled with wasi-sdk.
# No Emscripten, no JavaScript. All I/O through WASI fd_read/fd_write.
#
# Usage:
#   make                          # build
#   wasmtime build-standard/micropython.wasm  # run REPL
################################################################################

VARIANT ?= standard
VARIANT_DIR ?= variants/$(VARIANT)
BUILD ?= build-$(VARIANT)

# ── wasi-sdk paths ──
WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-30.0-x86_64-linux
WASI_SYSROOT = $(WASI_SDK)/share/wasi-sysroot
WASI_CC = $(WASI_SDK)/bin/clang-21

include ../../py/mkenv.mk

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

# Not a full CircuitPython build, but py.mk still needs these
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL ?= 0
USB_NUM_ENDPOINT_PAIRS ?= 0
CIRCUITPY_USB_DEVICE ?= 0

LDFLAGS += --target=wasm32-wasi
LDFLAGS += --sysroot=$(WASI_SYSROOT)
LDFLAGS += -mllvm -wasm-enable-sjlj
# Link setjmp/longjmp support (WASM Exception Handling based)
LDFLAGS += -lsetjmp
# Initial memory: 2MB (static 256KB heap + stack + data)
LDFLAGS += -Wl,--initial-memory=2097152

################################################################################
# Source files — variant-dependent

# Common HAL
SRC_C_COMMON = \
	wasi_mphal.c \

# Variant-specific main
ifeq ($(VARIANT),opfs)
SRC_C = \
	$(SRC_C_COMMON) \
	main_opfs.c \
	opfs_state.c \


else
SRC_C = \
	$(SRC_C_COMMON) \
	main.c \
	gccollect.c \

endif

SRC_SHARED = $(addprefix shared/,\
	runtime/gchelper_generic.c \
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

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
