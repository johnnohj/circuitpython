# Frozen manifest for ws-worker variant.
#
# Same Adafruit libraries as browser (bus_device, register, display,
# HID, etc.) but replaces the upstream neopixel with our protocol-
# wrapping version.

include("$(PORT_DIR)/variants/manifest.py")
include("$(PORT_DIR)/variants/browser/manifest_libs.py")

# WS protocol bus
freeze("modules", "_ws_bus.py")

# Protocol wrappers — shadow C modules with broadcasting versions
freeze("modules", "digitalio.py")
freeze("modules", "analogio.py")
freeze("modules", "busio.py")
freeze("modules", "pwmio.py")
freeze("modules", "rotaryio.py")
freeze("modules", "neopixel.py")
