# analogio.py — WS protocol wrapper for analogio.
#
# Shadows the C module (registered as _analogio).  Wraps AnalogIn
# to broadcast readings and AnalogOut to broadcast writes.

from _analogio import *
from _analogio import AnalogIn as _AnalogIn
from _analogio import AnalogOut as _AnalogOut
import _ws_bus

class AnalogIn(_AnalogIn):
    """AnalogIn with WS protocol broadcasting."""

    def __init__(self, pin):
        super().__init__(pin)
        self._pin_name = str(pin).split('.')[-1].rstrip(')')
        self._pin_id = pin.id if hasattr(pin, 'id') else -1
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='CREATE',
            mode='ANALOG')

    @_AnalogIn.value.getter
    def value(self):
        v = _AnalogIn.value.fget(self)
        _ws_bus.emit(_ws_bus.PIN_EVENT,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            pin_value=str(v))
        return v

    def deinit(self):
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='DELETE')
        super().deinit()


class AnalogOut(_AnalogOut):
    """AnalogOut with WS protocol broadcasting."""

    def __init__(self, pin):
        super().__init__(pin)
        self._pin_name = str(pin).split('.')[-1].rstrip(')')
        self._pin_id = pin.id if hasattr(pin, 'id') else -1
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='CREATE',
            mode='ANALOG')

    @_AnalogOut.value.setter
    def value(self, v):
        _AnalogOut.value.fset(self, v)
        _ws_bus.emit(_ws_bus.PIN_EVENT,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            pin_value=str(v))

    def deinit(self):
        _ws_bus.emit(_ws_bus.PIN_CONFIG,
            pin_name=self._pin_name,
            pin_id=self._pin_id,
            request_type='DELETE')
        super().deinit()
