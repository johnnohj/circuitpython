# Browser variant — adds the display pipeline to the base build.
# Hardware modules (digitalio, analogio, busio, etc.) are in the
# base Makefile — shared by both variants.

FROZEN_MANIFEST ?= $(VARIANT_DIR)/manifest.py

# ── DisplayIO rendering pipeline ──
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
	supervisor/status_bar.c \
	common-hal/displayio/__init__.c

# ── Display utility modules (browser only) ──
CFLAGS += -DCIRCUITPY_VECTORIO=1
CFLAGS += -DCIRCUITPY_BITMAPTOOLS=1
SRC_C += \
	shared-bindings/vectorio/__init__.c \
	shared-bindings/vectorio/Circle.c \
	shared-bindings/vectorio/Polygon.c \
	shared-bindings/vectorio/Rectangle.c \
	shared-bindings/vectorio/VectorShape.c \
	shared-module/vectorio/__init__.c \
	shared-module/vectorio/Circle.c \
	shared-module/vectorio/Polygon.c \
	shared-module/vectorio/Rectangle.c \
	shared-module/vectorio/VectorShape.c \
	shared-bindings/bitmaptools/__init__.c \
	shared-module/bitmaptools/__init__.c
