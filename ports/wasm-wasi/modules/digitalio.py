# digitalio.py — CircuitPython digitalio shim (U2IF protocol)
#
# Emits U2IF-compatible commands via _hw.send() instead of
# maintaining a state blob. Each operation is a single 64-byte
# command packet routed through weBlinka.

import _hw

# U2IF GPIO opcodes
_GPIO_INIT      = 0x20
_GPIO_SET_VALUE = 0x21
_GPIO_GET_VALUE = 0x22

# GPIO modes
_MODE_IN  = 0x00
_MODE_OUT = 0x01

# GPIO pulls
_PULL_NONE = 0x00
_PULL_UP   = 0x01
_PULL_DOWN = 0x02


class Direction:
    INPUT = 0
    OUTPUT = 1


class Pull:
    UP = 1
    DOWN = 2
    NONE = None


class DriveMode:
    PUSH_PULL = 0
    OPEN_DRAIN = 1


class DigitalInOut:
    def __init__(self, pin):
        if isinstance(pin, str):
            import board
            pin = getattr(board, pin)
        self._pin = pin
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        self._direction = Direction.INPUT
        self._pull = _PULL_NONE
        self._value = 0
        # Init as input, no pull
        _hw.send(_GPIO_INIT, self._pin_num, _MODE_IN, _PULL_NONE)

    def deinit(self):
        if self._pin is not None:
            _hw.send(_GPIO_INIT, self._pin_num, _MODE_IN, _PULL_NONE)
            self._pin = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    def switch_to_output(self, value=False, drive_mode=DriveMode.PUSH_PULL):
        self.direction = Direction.OUTPUT
        self.drive_mode = drive_mode
        self.value = value

    def switch_to_input(self, pull=None):
        self.direction = Direction.INPUT
        self.pull = pull

    @property
    def direction(self):
        return self._direction

    @direction.setter
    def direction(self, val):
        self._direction = val
        mode = _MODE_OUT if val == Direction.OUTPUT else _MODE_IN
        pull = self._pull if val == Direction.INPUT else _PULL_NONE
        _hw.send(_GPIO_INIT, self._pin_num, mode, pull)

    @property
    def value(self):
        rsp = _hw.query(_GPIO_GET_VALUE, self._pin_num)
        if rsp:
            return bool(rsp[2])
        return bool(self._value)

    @value.setter
    def value(self, val):
        self._value = 1 if val else 0
        _hw.send(_GPIO_SET_VALUE, self._pin_num, self._value)

    @property
    def pull(self):
        p = self._pull
        if p == _PULL_NONE:
            return None
        return p

    @pull.setter
    def pull(self, val):
        self._pull = val if val is not None else _PULL_NONE
        _hw.send(_GPIO_INIT, self._pin_num, _MODE_IN, self._pull)

    @property
    def drive_mode(self):
        return self._drive_mode if hasattr(self, '_drive_mode') else DriveMode.PUSH_PULL

    @drive_mode.setter
    def drive_mode(self, val):
        self._drive_mode = val
