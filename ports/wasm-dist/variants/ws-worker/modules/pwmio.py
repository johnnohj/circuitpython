# pwmio.py — WS protocol wrapper for pwmio.
#
# Maps to Wippersnapper protobuf:
#   pwm.proto (PWMPinAttachRequest, PWMPinWriteFrequencyDuty)
#   servo.proto (ServoAttachRequest, ServoWriteRequest)

from _pwmio import *
from _pwmio import PWMOut as _PWMOut
import _ws_bus


class PWMOut(_PWMOut):
    """PWMOut with WS protocol broadcasting."""

    def __init__(self, pin, *, duty_cycle=0, frequency=500, variable_frequency=False):
        super().__init__(pin, duty_cycle=duty_cycle,
                         frequency=frequency, variable_frequency=variable_frequency)
        self._pin_name = str(pin).split('.')[-1].rstrip(')')
        self._pin_id = pin.id if hasattr(pin, 'id') else -1
        _ws_bus.emit('pwm_attach',
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            frequency=frequency,
            duty_cycle=duty_cycle)

    @_PWMOut.duty_cycle.setter
    def duty_cycle(self, dc):
        _PWMOut.duty_cycle.fset(self, dc)
        _ws_bus.emit('pwm_write',
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            duty_cycle=dc)

    @_PWMOut.frequency.setter
    def frequency(self, freq):
        _PWMOut.frequency.fset(self, freq)
        _ws_bus.emit('pwm_write',
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            frequency=freq)

    def deinit(self):
        _ws_bus.emit('pwm_detach',
            pin_name=self._pin_name,
            pin_id=self._pin_id)
        super().deinit()
