# analogio.py -- CircuitPython analogio shim for WASI reactor
#
# Wire format per pin: [value:u16le][is_output:u8][enabled:u8] = 4 bytes
# 64 pins × 4 bytes = 256 bytes at /hw/analog/state

import struct
import os

_ANALOG_PATH = "/hw/analog/state"
_PIN_SIZE = 4
_FMT = "<HBB"  # value(u16le), is_output(u8), enabled(u8)

try:
    os.mkdir("/hw")
except OSError:
    pass
try:
    os.mkdir("/hw/analog")
except OSError:
    pass


def _read_pin(pin_num):
    try:
        f = open(_ANALOG_PATH, "rb")
        f.seek(pin_num * _PIN_SIZE)
        data = f.read(_PIN_SIZE)
        f.close()
        if len(data) == _PIN_SIZE:
            return struct.unpack(_FMT, data)
    except OSError:
        pass
    return (32768, 0, 0)


def _write_pin(pin_num, value, is_output, enabled):
    entry = struct.pack(_FMT, value, is_output, enabled)
    try:
        need = (pin_num + 1) * _PIN_SIZE
        try:
            st = os.stat(_ANALOG_PATH)
            if st[6] < need:
                f = open(_ANALOG_PATH, "ab")
                f.write(b'\x00' * (need - st[6]))
                f.close()
        except OSError:
            f = open(_ANALOG_PATH, "wb")
            f.write(b'\x00' * need)
            f.close()

        f = open(_ANALOG_PATH, "r+b")
        f.seek(pin_num * _PIN_SIZE)
        f.write(entry)
        f.close()
    except OSError:
        pass


class AnalogIn:
    def __init__(self, pin):
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        _write_pin(self._pin_num, 32768, 0, 1)

    def deinit(self):
        _write_pin(self._pin_num, 0, 0, 0)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    @property
    def value(self):
        return _read_pin(self._pin_num)[0]

    @property
    def reference_voltage(self):
        return 3.3


class AnalogOut:
    def __init__(self, pin):
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        _write_pin(self._pin_num, 0, 1, 1)

    def deinit(self):
        _write_pin(self._pin_num, 0, 0, 0)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    @property
    def value(self):
        return _read_pin(self._pin_num)[0]

    @value.setter
    def value(self, val):
        _write_pin(self._pin_num, val & 0xFFFF, 1, 1)
