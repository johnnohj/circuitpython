# neopixel_write.py -- CircuitPython neopixel_write shim for WASI reactor
#
# Wire format at /hw/neopixel/data:
#   [count:u8] followed by count × [pin:u8][num_bytes:u16le][pixels...]

import struct
import os

_NEO_PATH = "/hw/neopixel/data"

try:
    os.mkdir("/hw")
except OSError:
    pass
try:
    os.mkdir("/hw/neopixel")
except OSError:
    pass


def neopixel_write(digitalinout, buf):
    """Write NeoPixel data to the OPFS endpoint.

    digitalinout: a digitalio.DigitalInOut (or object with _pin_num)
    buf: bytearray of pixel data (GRB or GRBW bytes)
    """
    pin_num = 0
    if hasattr(digitalinout, '_pin_num'):
        pin_num = digitalinout._pin_num
    elif hasattr(digitalinout, 'pin') and hasattr(digitalinout.pin, 'number'):
        pin_num = digitalinout.pin.number

    num_bytes = len(buf)
    # Write single-pin format: [count=1][pin][num_bytes:u16le][pixels]
    header = struct.pack("<BBH", 1, pin_num, num_bytes)
    try:
        f = open(_NEO_PATH, "wb")
        f.write(header)
        f.write(buf)
        f.close()
    except OSError:
        pass
