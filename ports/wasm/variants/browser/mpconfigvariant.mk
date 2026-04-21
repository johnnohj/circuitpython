# Browser variant — the board running in a browser.
# Adds displayio, common-hal, and the supervisor terminal to standard.

FROZEN_MANIFEST ?= $(VARIANT_DIR)/manifest.py

# ── DisplayIO rendering pipeline ──
# Supervisor terminal (REPL + Blinka logo) renders through this.
CIRCUITPY_DISPLAYIO = 1

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
	shared-module/fontio/BuiltinFont.c

# Display resources: built-in font bitmap + Blinka sprite
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

SRC_C += $(SRC_DISPLAYIO)

# ── Port-specific display files ──
SRC_C += \
	wasm_framebuffer.c \
	board_display.c \
	supervisor/display.c \
	supervisor/status_bar.c

# ── Common-HAL sources ──
SRC_C += \
	common-hal/digitalio/DigitalInOut.c \
	common-hal/analogio/AnalogIn.c \
	common-hal/analogio/AnalogOut.c \
	common-hal/pwmio/PWMOut.c \
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
	common-hal/displayio/__init__.c \
	shared/runtime/context_manager_helpers.c \
	shared/runtime/buffer_helper.c

# ── Shared-bindings (register Python modules) ──
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

# ── CIRCUITPY flags ──
CFLAGS += \
	-DCIRCUITPY_DISPLAYIO=1 \
	-DCIRCUITPY_FRAMEBUFFERIO=1 \
	-DCIRCUITPY_TERMINALIO=1 \
	-DCIRCUITPY_FONTIO=1 \
	-DCIRCUITPY_REPL_LOGO=1 \
	-DCIRCUITPY_DIGITALIO=1 \
	-DCIRCUITPY_ANALOGIO=1 \
	-DCIRCUITPY_PWMIO=1 \
	-DCIRCUITPY_NEOPIXEL_WRITE=1 \
	-DCIRCUITPY_MICROCONTROLLER=1 \
	-DCIRCUITPY_BOARD=1 \
	-DCIRCUITPY_BUSIO=1 \
	-DCIRCUITPY_BUSIO_I2C=1 \
	-DCIRCUITPY_BUSIO_SPI=1 \
	-DCIRCUITPY_BUSIO_UART=1

# busio Make-level flag (for circuitpy_defns.mk SRC_PATTERNS)
CIRCUITPY_BUSIO = 1

# ── Pure software modules (no common-hal needed) ──
# These are commonly available on popular boards and essential
# for running Adafruit Learn Guide examples.

# rainbowio — colorwheel() used in nearly every NeoPixel guide
CIRCUITPY_RAINBOWIO = 1
CFLAGS += -DCIRCUITPY_RAINBOWIO=1
SRC_C += shared-bindings/rainbowio/__init__.c shared-module/rainbowio/__init__.c

# keypad — deferred (needs supervisor_acquire_lock, port_malloc_zero stubs)

# touchio — software capacitive touch (uses analogio internally)
CIRCUITPY_TOUCHIO = 1
CFLAGS += -DCIRCUITPY_TOUCHIO=1 -DCIRCUITPY_TOUCHIO_USE_NATIVE=0
SRC_C += \
	shared-bindings/touchio/__init__.c \
	shared-bindings/touchio/TouchIn.c \
	shared-module/touchio/TouchIn.c
