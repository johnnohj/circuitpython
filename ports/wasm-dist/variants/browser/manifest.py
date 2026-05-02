include("$(PORT_DIR)/variants/manifest.py")
include("$(PORT_DIR)/variants/browser/manifest_libs.py")

# Adafruit NeoPixel library (wraps neopixel_write common-hal).
# Use the frozen Adafruit version, not micropython-lib (which imports `machine`).
module("neopixel.py", "../../../../frozen/Adafruit_CircuitPython_NeoPixel", opt=3),
