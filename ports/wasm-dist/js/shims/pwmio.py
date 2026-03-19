# pwmio.py — CircuitPython PWM shim (wasm-dist simulator)
#
# Event format:
#   {"type":"hw","cmd":"pwm_init",   "pin":"D5","duty_cycle":0,"frequency":500}
#   {"type":"hw","cmd":"pwm_update", "pin":"D5","duty_cycle":32768,"frequency":500}
#   {"type":"hw","cmd":"pwm_deinit", "pin":"D5"}

import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))


class PWMOut:
    def __init__(self, pin, duty_cycle=0, frequency=500, variable_frequency=False):
        self._pin = pin
        self._duty_cycle = duty_cycle & 0xFFFF
        self._frequency = frequency
        self._variable_frequency = variable_frequency
        _hw({"type": "hw", "cmd": "pwm_init",
             "pin": self._pin, "duty_cycle": self._duty_cycle,
             "frequency": self._frequency})

    @property
    def duty_cycle(self):
        return self._duty_cycle

    @duty_cycle.setter
    def duty_cycle(self, val):
        self._duty_cycle = val & 0xFFFF
        _hw({"type": "hw", "cmd": "pwm_update",
             "pin": self._pin, "duty_cycle": self._duty_cycle,
             "frequency": self._frequency})

    @property
    def frequency(self):
        return self._frequency

    @frequency.setter
    def frequency(self, val):
        if not self._variable_frequency:
            raise ValueError("frequency is read-only; set variable_frequency=True")
        self._frequency = val
        _hw({"type": "hw", "cmd": "pwm_update",
             "pin": self._pin, "duty_cycle": self._duty_cycle,
             "frequency": self._frequency})

    def deinit(self):
        _hw({"type": "hw", "cmd": "pwm_deinit", "pin": self._pin})

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()
