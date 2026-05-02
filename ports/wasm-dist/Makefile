# ports/wasm-tmp Makefile — adapted from ports/wasm/Makefile
#
# Both variants (standard + browser) share the same common-hal hardware
# modules.  The only difference is whether the display pipeline is included.
#   standard: CLI REPL, no display, no board.svg
#   browser:  display pipeline (framebufferio + terminalio + Blinka), board UI

# Select the variant to build for:
ifdef VARIANT_DIR
VARIANT ?= $(notdir $(VARIANT_DIR:/=))
else
VARIANT ?= standard
VARIANT_DIR ?= variants/$(VARIANT)
endif

ifeq ($(wildcard $(VARIANT_DIR)/.),)
$(error Invalid VARIANT specified: $(VARIANT_DIR))
endif

BUILD ?= build-$(VARIANT)

include ../../py/mkenv.mk
-include mpconfigport.mk
include $(VARIANT_DIR)/mpconfigvariant.mk

FROZEN_MANIFEST ?= variants/manifest.py

PROG ?= circuitpython.wasm

# qstr definitions
QSTR_DEFS += qstrdefsport.h
QSTR_GLOBAL_DEPENDENCIES += $(VARIANT_DIR)/mpconfigvariant.h

include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)

# ── wasi-sdk ──
WASI_SDK ?= $(HOME)/.local/bin/wasi-sdk-32.0-x86_64-linux
WASI_SYSROOT = $(WASI_SDK)/share/wasi-sysroot
WASI_CC = $(WASI_SDK)/bin/clang-22
CC  = $(WASI_CC)
LD  = $(WASI_CC)

# compiler settings
CWARN = -Wall
CWARN += -Wextra -Wno-unused-parameter -Wpointer-arith -Wdouble-promotion -Wfloat-conversion -Wno-missing-field-initializers
CWARN += -Wno-incompatible-function-pointer-types
CFLAGS += $(INC) $(CWARN) -std=gnu99 -I$(VARIANT_DIR) $(CFLAGS_EXTRA)
CFLAGS += --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT)
CFLAGS += -D_WASI_EMULATED_PROCESS_CLOCKS

# CircuitPython module flags
CFLAGS += -DCIRCUITPY_WARNINGS=1
CFLAGS += -DCIRCUITPY_GETPASS=1
CFLAGS += -DCIRCUITPY_MSGPACK=1
CFLAGS += -DCIRCUITPY_AESIO=1
CFLAGS += -DCIRCUITPY_LOCALE=1
CFLAGS += -DCIRCUITPY_PIXELBUF=1
CFLAGS += -DCIRCUITPY_STORAGE=1

# Hardware module flags — always enabled (both variants)
CFLAGS += \
	-DCIRCUITPY_DIGITALIO=1 \
	-DCIRCUITPY_ANALOGIO=1 \
	-DCIRCUITPY_PWMIO=1 \
	-DCIRCUITPY_NEOPIXEL_WRITE=1 \
	-DCIRCUITPY_MICROCONTROLLER=1 \
	-DCIRCUITPY_BOARD=1 \
	-DCIRCUITPY_BUSIO=1 \
	-DCIRCUITPY_BUSIO_I2C=1 \
	-DCIRCUITPY_BUSIO_SPI=1 \
	-DCIRCUITPY_BUSIO_UART=1 \
	-DCIRCUITPY_RAINBOWIO=1 \
	-DCIRCUITPY_TOUCHIO=1 \
	-DCIRCUITPY_TOUCHIO_USE_NATIVE=0 \
	-DCIRCUITPY_ROTARYIO=1 \
	-DCIRCUITPY_COUNTIO=1 \
	-DCIRCUITPY_KEYPAD=0

CIRCUITPY_BUSIO = 1

$(BUILD)/shared-module/msgpack/__init__.o: CFLAGS += -include supervisor/msgpack_fixup.h

# WASM exception handling for NLR
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

STRIP =
SIZE = true

# Linker
# Reactor model: no _start, JS calls chassis_init + chassis_frame.
# _initialize is called automatically by WASI runtimes on instantiation.
LDFLAGS += --target=wasm32-wasip1 --sysroot=$(WASI_SYSROOT)
LDFLAGS += -mexec-model=reactor
LDFLAGS += -mllvm -wasm-enable-sjlj -fwasm-exceptions
LDFLAGS += -lsetjmp -lwasi-emulated-process-clocks
LDFLAGS += -Wl,--initial-memory=4194304
LDFLAGS += -Wl,-z,stack-size=524288
LDFLAGS += -Wl,--export=malloc -Wl,--export=free
LDFLAGS += $(LDFLAGS_MOD) $(LDFLAGS_EXTRA)

ifeq ($(MICROPY_USE_READLINE),1)
INC += -I$(TOP)/shared/readline
CFLAGS += -DMICROPY_USE_READLINE=1
SHARED_SRC_C_EXTRA += readline/readline.c
endif

# ── Source files ──
# Port layer
SRC_C += \
	port/main.c \
	port/ffi_exports.c \
	port/port_memory.c \
	port/budget.c \
	port/serial.c \
	port/port_serial.c \
	port/hal.c \
	port/vm_abort.c \
	port/port_step.c \
	port/event_ring.c \
	port/wasi_mphal.c \
	supervisor/port.c \
	supervisor/stubs.c \
	supervisor/compile.c \
	supervisor/shared/serial.c \
	supervisor/micropython.c \
	supervisor/background_callback.c \
	supervisor/tick.c \
	gccollect.c \
	mpthreadport.c \
	modringio.c \
	ffi/proxy_c.c \
	ffi/objjsproxy.c \
	ffi/modjsffi.c \
	shared-module/os/__init__.c \
	supervisor/shared/translate/translate.c \
	$(SRC_MOD) \
	$(wildcard $(VARIANT_DIR)/*.c)

# CircuitPython time module
SRC_C += \
	shared-bindings/time/__init__.c \
	shared-module/time/__init__.c

# Pure-software CircuitPython modules
SRC_C += \
	shared-bindings/warnings/__init__.c \
	shared-module/warnings/__init__.c \
	shared-bindings/getpass/__init__.c \
	shared-module/getpass/__init__.c \
	shared-bindings/aesio/__init__.c \
	shared-bindings/aesio/aes.c \
	shared-module/aesio/__init__.c \
	shared-module/aesio/aes.c \
	shared-bindings/locale/__init__.c \
	shared-bindings/adafruit_pixelbuf/__init__.c \
	shared-bindings/adafruit_pixelbuf/PixelBuf.c \
	shared-module/adafruit_pixelbuf/__init__.c \
	shared-module/adafruit_pixelbuf/PixelBuf.c \
	shared-bindings/storage/__init__.c \
	common-hal/storage/__init__.c \
	shared-bindings/msgpack/__init__.c \
	shared-bindings/msgpack/ExtType.c \
	shared-module/msgpack/__init__.c \
	shared-bindings/rainbowio/__init__.c \
	shared-module/rainbowio/__init__.c \
	shared-bindings/touchio/__init__.c \
	shared-bindings/touchio/TouchIn.c \
	shared-module/touchio/TouchIn.c \
	shared-bindings/rotaryio/__init__.c \
	shared-bindings/rotaryio/IncrementalEncoder.c \
	shared-bindings/countio/__init__.c \
	shared-bindings/countio/Counter.c \
	shared-bindings/countio/Edge.c \

# ── Common-HAL hardware modules (both variants) ──
SRC_C += \
	common-hal/digitalio/DigitalInOut.c \
	common-hal/analogio/AnalogIn.c \
	common-hal/analogio/AnalogOut.c \
	common-hal/pwmio/PWMOut.c \
	common-hal/rotaryio/IncrementalEncoder.c \
	common-hal/countio/Counter.c \
	common-hal/neopixel_write/__init__.c \
	common-hal/microcontroller/__init__.c \
	common-hal/microcontroller/Pin.c \
	common-hal/microcontroller/Processor.c \
	common-hal/board/__init__.c \
	common-hal/board/board_pins.c \
	common-hal/busio/I2C.c \
	common-hal/busio/SPI.c \
	common-hal/busio/UART.c \
	common-hal/os/__init__.c \
	shared/runtime/context_manager_helpers.c \
	shared/runtime/buffer_helper.c \
	shared-bindings/util.c

# ── Shared-bindings for hardware modules (both variants) ──
SRC_C += \
	shared-bindings/digitalio/__init__.c \
	shared-bindings/digitalio/DigitalInOut.c \
	shared-bindings/digitalio/DigitalInOutProtocol.c \
	shared-bindings/digitalio/Direction.c \
	shared-bindings/digitalio/DriveMode.c \
	shared-bindings/digitalio/Pull.c \
	shared-bindings/analogio/__init__.c \
	shared-bindings/analogio/AnalogIn.c \
	shared-bindings/analogio/AnalogOut.c \
	shared-bindings/pwmio/__init__.c \
	shared-bindings/pwmio/PWMOut.c \
	shared-bindings/busio/__init__.c \
	shared-bindings/busio/I2C.c \
	shared-bindings/busio/SPI.c \
	shared-bindings/busio/UART.c \
	shared-bindings/microcontroller/__init__.c \
	shared-bindings/microcontroller/Pin.c \
	shared-bindings/microcontroller/Processor.c \
	shared-bindings/microcontroller/ResetReason.c \
	shared-bindings/microcontroller/RunMode.c \
	shared-bindings/board/__init__.c \
	shared-bindings/neopixel_write/__init__.c \
	shared-module/board/__init__.c

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

SRC_QSTR += $(SRC_C) $(SRC_CXX) $(SHARED_SRC_C)

ifneq ($(FROZEN_MANIFEST),)
CFLAGS += -DMICROPY_QSTR_EXTRA_POOL=mp_qstr_frozen_const_pool
CFLAGS += -DMICROPY_MODULE_FROZEN_MPY
CFLAGS += -DMPZ_DIG_SIZE=16
CFLAGS += -DMICROPY_MODULE_FROZEN_STR
endif

CXXFLAGS += $(filter-out -Wmissing-prototypes -Wold-style-definition -std=gnu99,$(CFLAGS) $(CXXFLAGS_MOD))

include $(TOP)/py/mkrules.mk

# ── Build directory rules for subdirectories ──
$(BUILD)/port/%.o: port/%.c | $(BUILD)/port
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/port:
	mkdir -p $(BUILD)/port

$(BUILD)/ffi/%.o: ffi/%.c | $(BUILD)/ffi
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/ffi:
	mkdir -p $(BUILD)/ffi

$(BUILD)/supervisor/%.o: supervisor/%.c | $(BUILD)/supervisor
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/supervisor:
	mkdir -p $(BUILD)/supervisor

.PHONY: test test-all

NODE ?= $(shell which node)

test: $(BUILD)/$(PROG)
	$(NODE) tests/test_standard.mjs
	$(NODE) tests/test_hardware.mjs

test-all: build-standard/circuitpython.wasm build-browser/circuitpython.wasm
	$(NODE) tests/test_standard.mjs
	$(NODE) tests/test_hardware.mjs
