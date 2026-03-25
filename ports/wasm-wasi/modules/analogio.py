# analogio.py — CircuitPython analogio shim (U2IF protocol)

import _hw

_ADC_INIT      = 0x40
_ADC_GET_VALUE = 0x41


class AnalogIn:
    def __init__(self, pin):
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        _hw.send(_ADC_INIT, self._pin_num)

    def deinit(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    @property
    def value(self):
        return _hw.query_u16(_ADC_GET_VALUE, self._pin_num, offset=3)

    @property
    def reference_voltage(self):
        return 3.3


class AnalogOut:
    def __init__(self, pin):
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        self._value = 0

    def deinit(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, val):
        self._value = val & 0xFFFF
        # No standard U2IF opcode for DAC output — use GPIO_SET_VALUE as proxy
        _hw.send_u16(0x21, self._pin_num, self._value)
