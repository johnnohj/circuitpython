USB_VID = 0x303A
USB_PID = 0x80FC
USB_PRODUCT = "MixGo CE"
USB_MANUFACTURER = "Espressif"

IDF_TARGET = esp32s2

CIRCUITPY_ESP_FLASH_MODE = qio
CIRCUITPY_ESP_FLASH_FREQ = 80m
CIRCUITPY_ESP_FLASH_SIZE = 4MB

FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_ConnectionManager
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_Requests
FROZEN_MPY_DIRS += $(TOP)/frozen/Adafruit_CircuitPython_NeoPixel
FROZEN_MPY_DIRS += $(TOP)/frozen/mixgo_cp_lib/mixgoce_lib

CIRCUITPY_MESSAGE_COMPRESSION_LEVEL = 9
