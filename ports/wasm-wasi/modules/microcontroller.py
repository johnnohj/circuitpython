# microcontroller.py -- CircuitPython microcontroller shim for WASI reactor
#
# Provides pin references and cpu properties. No real hardware control.

import sys


class Pin:
    """MCU-level pin reference (GPIO0-GPIO63)."""
    def __init__(self, number):
        self.number = number
    def __repr__(self):
        return "microcontroller.pin.GPIO{}".format(self.number)

# Pin namespace — microcontroller.pin.GPIO0 through GPIO63
class _PinModule:
    def __getattr__(self, name):
        if name.startswith("GPIO"):
            try:
                n = int(name[4:])
                if 0 <= n < 64:
                    return Pin(n)
            except ValueError:
                pass
        raise AttributeError(name)

pin = _PinModule()


class _Processor:
    """Virtual processor — frequency is settable for prototyping."""

    def __init__(self):
        self._frequency = 100_000_000  # 100 MHz default
        self._freq_noted = False

    @property
    def frequency(self):
        if not self._freq_noted:
            self._freq_noted = True
            print("[wasm] cpu.frequency is simulated ({} Hz)".format(
                self._frequency), file=sys.stderr)
        return self._frequency

    @frequency.setter
    def frequency(self, val):
        self._frequency = val
        self._freq_noted = False

    @property
    def temperature(self):
        return float('nan')

    @property
    def voltage(self):
        return float('nan')

    @property
    def uid(self):
        return b'\xaf'

    @property
    def reset_reason(self):
        return ""

cpu = _Processor()


def delay_us(us):
    """Microsecond delay — yields for long delays, no-op for short."""
    if us >= 1000:
        import time
        time.sleep(us / 1_000_000)


def disable_interrupts():
    pass

def enable_interrupts():
    pass
