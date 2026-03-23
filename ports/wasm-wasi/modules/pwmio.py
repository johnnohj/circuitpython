# pwmio.py -- CircuitPython pwmio shim for WASI reactor
#
# Wire format per pin (8 bytes):
#   [duty_cycle:u16le][frequency:u32le][flags:u8][_pad:u8]
#   flags: bit0=variable_freq, bit1=enabled, bit2=never_reset
# 64 pins × 8 bytes = 512 bytes at /hw/pwm/state

import struct
import os

_PWM_PATH = "/hw/pwm/state"
_PIN_SIZE = 8
_FMT = "<IHBx"  # Note: frequency is u32, duty is u16 — but wire is duty first
# Actually matches hw_opfs.c: [duty:u16][freq:u32][flags:u8][pad:u8]
_FMT = "<HIBx"

try:
    os.mkdir("/hw")
except OSError:
    pass
try:
    os.mkdir("/hw/pwm")
except OSError:
    pass


def _read_pin(pin_num):
    try:
        f = open(_PWM_PATH, "rb")
        f.seek(pin_num * _PIN_SIZE)
        data = f.read(_PIN_SIZE)
        f.close()
        if len(data) == _PIN_SIZE:
            duty, freq, flags = struct.unpack(_FMT, data)
            return duty, freq, flags
    except OSError:
        pass
    return (0, 500, 0)


def _write_pin(pin_num, duty, frequency, flags):
    entry = struct.pack(_FMT, duty, frequency, flags)
    try:
        need = (pin_num + 1) * _PIN_SIZE
        try:
            st = os.stat(_PWM_PATH)
            if st[6] < need:
                f = open(_PWM_PATH, "ab")
                f.write(b'\x00' * (need - st[6]))
                f.close()
        except OSError:
            f = open(_PWM_PATH, "wb")
            f.write(b'\x00' * need)
            f.close()

        f = open(_PWM_PATH, "r+b")
        f.seek(pin_num * _PIN_SIZE)
        f.write(entry)
        f.close()
    except OSError:
        pass


class PWMOut:
    def __init__(self, pin, duty_cycle=0, frequency=500, variable_frequency=False):
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        self._variable_frequency = variable_frequency
        flags = (0x01 if variable_frequency else 0) | 0x02  # enabled
        _write_pin(self._pin_num, duty_cycle, frequency, flags)

    def deinit(self):
        _write_pin(self._pin_num, 0, 0, 0)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    @property
    def duty_cycle(self):
        return _read_pin(self._pin_num)[0]

    @duty_cycle.setter
    def duty_cycle(self, val):
        _, freq, flags = _read_pin(self._pin_num)
        _write_pin(self._pin_num, val & 0xFFFF, freq, flags)

    @property
    def frequency(self):
        return _read_pin(self._pin_num)[1]

    @frequency.setter
    def frequency(self, val):
        duty, _, flags = _read_pin(self._pin_num)
        if not (flags & 0x01):
            raise ValueError("PWM frequency not writable when variable_frequency is False")
        _write_pin(self._pin_num, duty, val, flags)
