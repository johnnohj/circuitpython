# board.py -- CircuitPython board shim for WASI reactor
#
# Pin name constants matching the worker's board layout
# (common-hal/board/board_pins.c). These are lightweight
# objects that carry a pin number for the hardware shims.

class Pin:
    """Lightweight pin reference — just carries a number."""
    def __init__(self, number, name=""):
        self.number = number
        self._name = name
    def __repr__(self):
        return "board.{}".format(self._name) if self._name else "Pin({})".format(self.number)

# Digital pins
D0  = Pin(0,  "D0")
D1  = Pin(1,  "D1")
D2  = Pin(2,  "D2")
D3  = Pin(3,  "D3")
D4  = Pin(4,  "D4")
D5  = Pin(5,  "D5")
D6  = Pin(6,  "D6")
D7  = Pin(7,  "D7")
D8  = Pin(8,  "D8")
D9  = Pin(9,  "D9")
D10 = Pin(10, "D10")
D11 = Pin(11, "D11")
D12 = Pin(12, "D12")
D13 = Pin(13, "D13")

# Analog pins (aliases to D0-D5)
A0 = Pin(0, "A0")
A1 = Pin(1, "A1")
A2 = Pin(2, "A2")
A3 = Pin(3, "A3")
A4 = Pin(4, "A4")
A5 = Pin(5, "A5")

# I2C
SDA = Pin(8,  "SDA")
SCL = Pin(9,  "SCL")

# SPI
MOSI = Pin(10, "MOSI")
MISO = Pin(11, "MISO")
SCK  = Pin(12, "SCK")

# Built-in LED
LED = Pin(13, "LED")

# UART
TX = Pin(14, "TX")
RX = Pin(15, "RX")

# NeoPixel
NEOPIXEL = Pin(16, "NEOPIXEL")

# Button
BUTTON = Pin(17, "BUTTON")


def I2C():
    """Return default I2C bus."""
    import busio
    return busio.I2C(SCL, SDA)

def SPI():
    """Return default SPI bus."""
    import busio
    return busio.SPI(SCK, MOSI, MISO)

def UART():
    """Return default UART."""
    import busio
    return busio.UART(TX, RX)
