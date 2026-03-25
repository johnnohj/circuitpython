# pwmio.py — CircuitPython pwmio shim (U2IF protocol)

import _hw

_PWM_INIT        = 0x30
_PWM_DEINIT      = 0x31
_PWM_SET_FREQ    = 0x32
_PWM_GET_FREQ    = 0x33
_PWM_SET_DUTY    = 0x34
_PWM_GET_DUTY    = 0x35


class PWMOut:
    def __init__(self, pin, duty_cycle=0, frequency=500, variable_frequency=False):
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        self._variable_frequency = variable_frequency
        _hw.send(_PWM_INIT, self._pin_num)
        _hw.send_u32(_PWM_SET_FREQ, self._pin_num, frequency)
        _hw.send_u16(_PWM_SET_DUTY, self._pin_num, duty_cycle)

    def deinit(self):
        _hw.send(_PWM_DEINIT, self._pin_num)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    @property
    def duty_cycle(self):
        return _hw.query_u16(_PWM_GET_DUTY, self._pin_num, offset=2)

    @duty_cycle.setter
    def duty_cycle(self, val):
        _hw.send_u16(_PWM_SET_DUTY, self._pin_num, val)

    @property
    def frequency(self):
        rsp = _hw.query(_PWM_GET_FREQ, self._pin_num)
        if rsp and len(rsp) >= 6:
            import struct
            return struct.unpack_from("<I", rsp, 2)[0]
        return 500

    @frequency.setter
    def frequency(self, val):
        if not self._variable_frequency:
            raise ValueError("PWM frequency not writable when variable_frequency is False")
        _hw.send_u32(_PWM_SET_FREQ, self._pin_num, val)
