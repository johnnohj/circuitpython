# analogio.py — CircuitPython analog I/O shim (wasm-dist simulator)
#
# AnalogIn reads from the hardware register file (16-bit values, 0–65535).
# AnalogOut writes to the register file + emits bc_out event.
#
# Event format:
#   {"type":"hw","cmd":"analog_init",  "pin":"A0","direction":"input"}
#   {"type":"hw","cmd":"analog_write", "pin":"A0","value":32768}
#   {"type":"hw","cmd":"analog_deinit","pin":"A0"}

import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))


class AnalogIn:
    def __init__(self, pin):
        self._pin = pin
        self._reg_addr = _blinka.pin_to_reg(pin)
        _hw({"type": "hw", "cmd": "analog_init",
             "pin": self._pin, "direction": "input"})

    @property
    def value(self):
        if self._reg_addr >= 0:
            return _blinka.read_reg(self._reg_addr)
        return 0

    @property
    def reference_voltage(self):
        return 3.3

    def deinit(self):
        _hw({"type": "hw", "cmd": "analog_deinit", "pin": self._pin})

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()


class AnalogOut:
    def __init__(self, pin):
        self._pin = pin
        self._reg_addr = _blinka.pin_to_reg(pin)
        self._value = 0
        _hw({"type": "hw", "cmd": "analog_init",
             "pin": self._pin, "direction": "output"})

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, val):
        self._value = val & 0xFFFF
        if self._reg_addr >= 0:
            _blinka.write_reg(self._reg_addr, self._value)
        _hw({"type": "hw", "cmd": "analog_write",
             "pin": self._pin, "value": self._value})

    def deinit(self):
        _hw({"type": "hw", "cmd": "analog_deinit", "pin": self._pin})

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()
