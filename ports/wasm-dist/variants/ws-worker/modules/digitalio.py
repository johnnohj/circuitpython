# digitalio.py — WS protocol wrapper for digitalio.
#
# Shadows the C module (registered as _digitalio) and re-exports
# everything.  Wraps DigitalInOut to broadcast pin state changes
# via the WS protocol bus.
#
# User code is unchanged:
#   import digitalio
#   pin = digitalio.DigitalInOut(board.D13)
#   pin.direction = digitalio.Direction.OUTPUT
#   pin.value = True  # → emits pin_event to sync bus

from _digitalio import *
from _digitalio import DigitalInOut as _DigitalInOut
import _ws_bus

class DigitalInOut(_DigitalInOut):
    """DigitalInOut with WS protocol broadcasting."""

    def __init__(self, pin):
        super().__init__(pin)
        self._pin_name = str(pin).split('.')[-1].rstrip(')')
        self._pin_id = pin.id if hasattr(pin, 'id') else -1
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='CREATE',
            mode='DIGITAL')

    @_DigitalInOut.direction.setter
    def direction(self, d):
        _DigitalInOut.direction.fset(self, d)
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='UPDATE',
            direction='OUTPUT' if d == Direction.OUTPUT else 'INPUT')

    @_DigitalInOut.value.getter
    def value(self):
        v = _DigitalInOut.value.fget(self)
        return v

    @_DigitalInOut.value.setter
    def value(self, v):
        _DigitalInOut.value.fset(self, v)
        _ws_bus.emit(_ws_bus.PIN_EVENT,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            pin_value=str(v))

    @_DigitalInOut.pull.setter
    def pull(self, p):
        _DigitalInOut.pull.fset(self, p)
        pull_name = 'UP' if p == Pull.UP else 'DOWN' if p == Pull.DOWN else 'NONE'
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='UPDATE',
            pull=pull_name)

    def deinit(self):
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='DELETE')
        super().deinit()
