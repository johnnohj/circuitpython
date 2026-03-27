include("$(PORT_DIR)/variants/manifest.py")

# Adafruit frozen libraries — available via `import neopixel` etc.
# Single-file modules
# module("neopixel.py", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_NeoPixel", opt=3)
module("adafruit_ticks.py", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_Ticks", opt=3)
module("adafruit_framebuf.py", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_framebuf", opt=3)

# Multi-file packages
# package("adafruit_bus_device", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_BusDevice", opt=3)
# package("adafruit_register", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_Register", opt=3)
# adafruit_hid requires Blinka (__future__), not compatible with frozen CP
# adafruit_bus_device - I2C/SPI device abstractions (used by most sensor drivers)
package(
    "adafruit_bus_device",
    (
        "__init__.py",
        "i2c_device.py",
        "spi_device.py",
    ),
    base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_BusDevice",
    opt=3,
)

# adafruit_register - Register-based I2C device access
package(
    "adafruit_register",
    (
        "__init__.py",
        "i2c_bit.py",
        "i2c_bits.py",
        "i2c_struct.py",
        "i2c_struct_array.py",
        "i2c_bcd_alarm.py",
        "i2c_bcd_datetime.py",
    ),
    base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_Register",
    opt=3,
)
package("adafruit_display_text", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_Display_Text", opt=3)
package("adafruit_display_shapes", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_Display_Shapes", opt=3)
package("adafruit_bitmap_font", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_Bitmap_Font", opt=3)
package("adafruit_imageload", base_path="$(MPY_DIR)/frozen/Adafruit_CircuitPython_ImageLoad", opt=3)
