# rotaryio.py — WS protocol wrapper for rotaryio.
#
# Broadcasts encoder position changes.  Could be used to simulate
# rotary encoders in the browser UI or to observe physical encoder
# state over WebSerial.

from _rotaryio import *
from _rotaryio import IncrementalEncoder as _IncrementalEncoder
import _ws_bus


class IncrementalEncoder(_IncrementalEncoder):
    """IncrementalEncoder with WS protocol broadcasting."""

    def __init__(self, pin_a, pin_b, divisor=4):
        super().__init__(pin_a, pin_b, divisor=divisor)
        self._a_name = str(pin_a).split('.')[-1].rstrip(')')
        self._b_name = str(pin_b).split('.')[-1].rstrip(')')
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._a_name,
            request_type='CREATE',
            mode='ENCODER')

    @_IncrementalEncoder.position.getter
    def position(self):
        pos = _IncrementalEncoder.position.fget(self)
        _ws_bus.emit(_ws_bus.PIN_EVENT,
            pin_name=self._a_name,
            pin_value=str(pos))
        return pos

    def deinit(self):
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._a_name,
            request_type='DELETE')
        super().deinit()
