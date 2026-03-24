################################################################################
# WASI port Makefile
#
# Headless CircuitPython VM compiled with wasi-sdk.
# No Emscripten, no JavaScript. All I/O through WASI fd_read/fd_write.
#
# Variants:
#   standard — simple REPL on stdin/stdout (plain MicroPython)
#   opfs     — CIRCUITPY=1, supervisor infrastructure, OPFS-backed state
#   reactor  — externally-driven stepping, JS host is supervisor
#   worker   — hardware peripheral controller, owns display pipeline
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
include mpconfigport.mk

# CircuitPython module configuration (194 CIRCUITPY_* → CFLAGS)
# This is what makes us a CircuitPython port, not a MicroPython port.
# mpconfigboard.h provides board-level overrides; circuitpy_mpconfig.mk
# sets defaults and emits -DCIRCUITPY_FOO=$(CIRCUITPY_FOO) for each.
include $(TOP)/py/circuitpy_mpconfig.mk

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
# wasm32: uintptr_t is unsigned long, mp_uint_t is unsigned int (same size).
# Stream protocol function pointers trigger -Wincompatible-function-pointer-types.
CFLAGS += -Wno-incompatible-function-pointer-types
CFLAGS += -Os -DNDEBUG
CFLAGS += -D_POSIX_C_SOURCE=199309L
CFLAGS += $(INC)

# Exception handling: WASM native EH for setjmp/longjmp (NLR).
# -mllvm -wasm-enable-sjlj: transforms C setjmp/longjmp calls to use
#   WASM EH (__wasm_setjmp/__wasm_longjmp from libsetjmp.a).
# -fwasm-exceptions: enables WASM EH for C++ throw/catch (used by
#   nlrwasm.cpp for direct WASM throw in nlr_jump).
# Together they ensure ALL exception paths use WASM EH, not emulation.
# Requires a runtime that supports the WASM EH proposal
# (Node.js/V8, Chrome 91+, Firefox 100+, Safari 15.2+).
CFLAGS += -mllvm -wasm-enable-sjlj
CFLAGS += -fwasm-exceptions

# CircuitPython mode (same as unix port): enables CP code paths in py/*.c
CFLAGS += -DCIRCUITPY=1
CIRCUITPY_MESSAGE_COMPRESSION_LEVEL ?= 0
USB_NUM_ENDPOINT_PAIRS ?= 0
CIRCUITPY_USB_DEVICE ?= 0

LDFLAGS += --target=wasm32-wasi
LDFLAGS += --sysroot=$(WASI_SYSROOT)
LDFLAGS += -mllvm -wasm-enable-sjlj
LDFLAGS += -fwasm-exceptions
LDFLAGS += -lsetjmp
LDFLAGS += -Wl,--initial-memory=4194304
LDFLAGS += -Wl,-z,stack-size=524288

################################################################################
# Source files

# Common HAL (used by all variants)
SRC_C_COMMON = \
	wasi_mphal.c \
	modringio.c \

# CircuitPython supervisor (shared across variants)
SRC_SUPERVISOR_COMMON = \
	supervisor/shared/background_callback.c \
	supervisor/shared/translate/translate.c \
	supervisor/stub/filesystem.c \
	supervisor/stub/safe_mode.c \
	supervisor/stub/stack.c \

# ---- displayio / terminalio / fontio ----
# When CIRCUITPY_DISPLAYIO=1, add the display rendering pipeline.
# The supervisor terminal (Blinka logo + REPL) renders through this.
ifeq ($(CIRCUITPY_DISPLAYIO),1)
SRC_SUPERVISOR_COMMON += supervisor/shared/display.c

SRC_DISPLAYIO = \
	shared-bindings/displayio/__init__.c \
	shared-bindings/displayio/Bitmap.c \
	shared-bindings/displayio/ColorConverter.c \
	shared-bindings/displayio/Colorspace.c \
	shared-bindings/displayio/Group.c \
	shared-bindings/displayio/Palette.c \
	shared-bindings/displayio/TileGrid.c \
	shared-bindings/displayio/area.c \
	shared-bindings/util.c \
	shared-module/displayio/__init__.c \
	shared-module/displayio/Bitmap.c \
	shared-module/displayio/ColorConverter.c \
	shared-module/displayio/Group.c \
	shared-module/displayio/Palette.c \
	shared-module/displayio/TileGrid.c \
	shared-module/displayio/area.c \
	shared-module/displayio/bus_core.c \
	shared-module/displayio/display_core.c \
	shared-bindings/framebufferio/__init__.c \
	shared-bindings/framebufferio/FramebufferDisplay.c \
	shared-module/framebufferio/__init__.c \
	shared-module/framebufferio/FramebufferDisplay.c \
	shared-bindings/terminalio/__init__.c \
	shared-bindings/terminalio/Terminal.c \
	shared-module/terminalio/__init__.c \
	shared-module/terminalio/Terminal.c \
	shared-bindings/fontio/__init__.c \
	shared-bindings/fontio/BuiltinFont.c \
	shared-bindings/fontio/Glyph.c \
	shared-module/fontio/__init__.c \
	shared-module/fontio/BuiltinFont.c \

# Display resources: built-in font bitmap + Blinka sprite.
# Generated by gen_display_resources.py at build time.
TRANSLATION ?= en_US
CIRCUITPY_DISPLAY_FONT ?= "../../tools/fonts/ter-u12n.bdf"
CIRCUITPY_FONT_EXTRA_CHARACTERS ?= ""

AUTOGEN_DISPLAY = $(BUILD)/autogen_display_resources-$(TRANSLATION).c
SRC_DISPLAYIO += $(AUTOGEN_DISPLAY)

$(AUTOGEN_DISPLAY): ../../tools/gen_display_resources.py $(TOP)/locale/$(TRANSLATION).po Makefile | $(HEADER_BUILD)
	$(STEPECHO) "GEN $@"
	$(Q)install -d $(BUILD)/genhdr
	$(Q)$(PYTHON) ../../tools/gen_display_resources.py \
		--font $(CIRCUITPY_DISPLAY_FONT) \
		--sample_file $(TOP)/locale/$(TRANSLATION).po \
		--extra_characters $(CIRCUITPY_FONT_EXTRA_CHARACTERS) \
		--output_c_file $@
else
SRC_DISPLAYIO =
endif

# tick.c provides mp_hal_delay_ms and supervisor tick counters.
# The reactor variant provides its own yield-based delay, so it
# excludes tick.c and stubs the few functions other code needs.
ifneq ($(VARIANT),reactor)
SRC_SUPERVISOR = $(SRC_SUPERVISOR_COMMON) supervisor/shared/tick.c
else
SRC_SUPERVISOR = $(SRC_SUPERVISOR_COMMON)
endif

# FatFS config (supervisor/filesystem.h includes vfs_fat.h)
INC += -I$(TOP)/lib/oofatfs
CFLAGS += -DFFCONF_H=\"ffconf.h\"

# ---- Variant-specific sources ----

ifeq ($(VARIANT),opfs)

SRC_C = \
	$(SRC_C_COMMON) \
	main_opfs.c \
	opfs_state.c \
	dev_repl.c \
	supervisor/port.c \

else ifeq ($(VARIANT),reactor)

SRC_C = \
	$(SRC_C_COMMON) \
	main_reactor.c \
	gccollect.c \
	supervisor/port.c \

else ifeq ($(VARIANT),worker)

SRC_C = \
	$(SRC_C_COMMON) \
	main_worker.c \
	gccollect.c \
	supervisor/port.c \

else

SRC_C = \
	$(SRC_C_COMMON) \
	main.c \
	gccollect.c \
	supervisor/port.c \

endif

# Display support (added to all variants when enabled)
ifeq ($(CIRCUITPY_DISPLAYIO),1)
SRC_C += \
	wasm_framebuffer.c \
	board_display.c \
	worker_terminal.c \
	supervisor_stubs.c \

SRC_COMMON_HAL_DISPLAY = \
	common-hal/displayio/__init__.c \

endif

SRC_SHARED_EXTRA = $(SRC_SUPERVISOR)

SRC_SHARED = $(addprefix shared/,\
	runtime/gchelper_generic.c \
	runtime/interrupt_char.c \
	runtime/stdout_helpers.c \
	runtime/pyexec.c \
	runtime/context_manager_helpers.c \
	runtime/buffer_helper.c \
	readline/readline.c \
	timeutils/timeutils.c \
	)

# Add supervisor sources (paths are relative to TOP)
SRC_SHARED += $(SRC_SHARED_EXTRA)

# CircuitPython shared-bindings + shared-module sources.
# These are the CircuitPython module implementations that call our
# common-hal functions. On real ports, circuitpy_defns.mk adds these
# automatically; we list them explicitly until we integrate that.
SRC_CIRCUITPY = \
	shared-bindings/time/__init__.c \
	shared-bindings/os/__init__.c \
	shared-bindings/random/__init__.c \
	shared-bindings/math/__init__.c \
	shared-bindings/struct/__init__.c \
	shared-module/time/__init__.c \
	shared-module/os/__init__.c \
	shared-module/random/__init__.c \
	shared-module/struct/__init__.c \
	shared-module/warnings/__init__.c \

# Hardware shared-bindings — worker only (reactor uses Python shims)
ifeq ($(VARIANT),worker)
SRC_CIRCUITPY += \
	shared-bindings/microcontroller/__init__.c \
	shared-bindings/microcontroller/Pin.c \
	shared-bindings/microcontroller/Processor.c \
	shared-bindings/microcontroller/RunMode.c \
	shared-bindings/microcontroller/ResetReason.c \
	shared-bindings/digitalio/__init__.c \
	shared-bindings/digitalio/DigitalInOut.c \
	shared-bindings/digitalio/Direction.c \
	shared-bindings/digitalio/DriveMode.c \
	shared-bindings/digitalio/Pull.c \
	shared-bindings/analogio/__init__.c \
	shared-bindings/analogio/AnalogIn.c \
	shared-bindings/analogio/AnalogOut.c \
	shared-bindings/pwmio/__init__.c \
	shared-bindings/pwmio/PWMOut.c \
	shared-bindings/neopixel_write/__init__.c \
	shared-bindings/busio/__init__.c \
	shared-bindings/busio/I2C.c \
	shared-bindings/busio/SPI.c \
	shared-bindings/busio/UART.c \
	shared-bindings/board/__init__.c \
	shared-module/board/__init__.c \

endif

# Port-specific common-hal (os is needed by all variants)
SRC_COMMON_HAL = \
	common-hal/os/__init__.c \

# Hardware common-hal — worker only (reads/writes OPFS endpoints).
# The reactor uses frozen Python shims instead.
ifeq ($(VARIANT),worker)
SRC_COMMON_HAL += \
	hw_opfs.c \
	common-hal/microcontroller/__init__.c \
	common-hal/microcontroller/Pin.c \
	common-hal/microcontroller/Processor.c \
	common-hal/digitalio/__init__.c \
	common-hal/digitalio/DigitalInOut.c \
	common-hal/analogio/__init__.c \
	common-hal/analogio/AnalogIn.c \
	common-hal/analogio/AnalogOut.c \
	common-hal/pwmio/__init__.c \
	common-hal/pwmio/PWMOut.c \
	common-hal/neopixel_write/__init__.c \
	common-hal/busio/__init__.c \
	common-hal/busio/I2C.c \
	common-hal/busio/SPI.c \
	common-hal/busio/UART.c \
	common-hal/board/__init__.c \
	common-hal/board/board_pins.c \

endif

SRC_QSTR += $(SRC_C) $(SRC_SHARED) $(SRC_CIRCUITPY) $(SRC_COMMON_HAL)
SRC_QSTR += $(SRC_DISPLAYIO) $(SRC_COMMON_HAL_DISPLAY)

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_SHARED:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_CIRCUITPY:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_COMMON_HAL:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_DISPLAYIO:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_COMMON_HAL_DISPLAY:.c=.o))

################################################################################
# Build targets

.PHONY: all clean test

all: $(BUILD)/circuitpython.wasm

$(BUILD)/circuitpython.wasm: $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJ)

test: $(BUILD)/circuitpython.wasm
	wasmtime $<

clean:
	$(RM) -rf $(BUILD)

################################################################################
include $(TOP)/py/mkrules.mk
