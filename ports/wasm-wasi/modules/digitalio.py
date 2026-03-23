# digitalio.py -- CircuitPython digitalio shim for WASI reactor
#
# Reads/writes GPIO state via /hw/gpio/state (binary file).
# Wire format per pin: [value:u8][direction:u8][pull:u8][open_drain:u8][enabled:u8][never_reset:u8]
# 64 pins × 6 bytes = 384 bytes total.
#
# The worker's C common-hal reads this file via hw_opfs_gpio_read().

import struct
import os

_GPIO_PATH = "/hw/gpio/state"
_PIN_SIZE = 6   # bytes per pin entry
_NUM_PINS = 64

# Ensure directories exist
try:
    os.mkdir("/hw")
except OSError:
    pass
try:
    os.mkdir("/hw/gpio")
except OSError:
    pass


def _read_pin(pin_num):
    """Read one pin's state from the GPIO state file."""
    try:
        f = open(_GPIO_PATH, "rb")
        f.seek(pin_num * _PIN_SIZE)
        data = f.read(_PIN_SIZE)
        f.close()
        if len(data) == _PIN_SIZE:
            return struct.unpack("6B", data)
    except OSError:
        pass
    return (0, 0, 0, 0, 0, 0)


def _write_pin(pin_num, value, direction, pull, open_drain, enabled, never_reset=0):
    """Write one pin's state to the GPIO state file."""
    entry = struct.pack("6B", value, direction, pull, open_drain, enabled, never_reset)
    try:
        # Ensure file exists and is large enough
        try:
            st = os.stat(_GPIO_PATH)
            need = (pin_num + 1) * _PIN_SIZE
            if st[6] < need:
                # Extend file
                f = open(_GPIO_PATH, "ab")
                f.write(b'\x00' * (need - st[6]))
                f.close()
        except OSError:
            # Create with zeros up to this pin
            f = open(_GPIO_PATH, "wb")
            f.write(b'\x00' * ((pin_num + 1) * _PIN_SIZE))
            f.close()

        f = open(_GPIO_PATH, "r+b")
        f.seek(pin_num * _PIN_SIZE)
        f.write(entry)
        f.close()
    except OSError:
        pass


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
            # Board pin name — look up number from board module
            import board
            pin = getattr(board, pin)
        self._pin = pin
        self._pin_num = pin if isinstance(pin, int) else pin.number if hasattr(pin, 'number') else int(pin)
        _write_pin(self._pin_num, 0, Direction.INPUT, 0, 0, 1)

    def deinit(self):
        if self._pin is not None:
            _write_pin(self._pin_num, 0, 0, 0, 0, 0)
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
        state = _read_pin(self._pin_num)
        return state[1]

    @direction.setter
    def direction(self, val):
        state = _read_pin(self._pin_num)
        _write_pin(self._pin_num, state[0], val, state[2], state[3], state[4])

    @property
    def value(self):
        state = _read_pin(self._pin_num)
        return bool(state[0])

    @value.setter
    def value(self, val):
        state = _read_pin(self._pin_num)
        _write_pin(self._pin_num, 1 if val else 0, state[1], state[2], state[3], state[4])

    @property
    def pull(self):
        state = _read_pin(self._pin_num)
        p = state[2]
        if p == 0:
            return None
        return p

    @pull.setter
    def pull(self, val):
        state = _read_pin(self._pin_num)
        _write_pin(self._pin_num, state[0], state[1], val if val is not None else 0, state[3], state[4])

    @property
    def drive_mode(self):
        state = _read_pin(self._pin_num)
        return state[3]

    @drive_mode.setter
    def drive_mode(self, val):
        state = _read_pin(self._pin_num)
        _write_pin(self._pin_num, state[0], state[1], state[2], val, state[4])
