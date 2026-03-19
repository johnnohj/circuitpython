# digitalio.py — CircuitPython digital I/O shim (wasm-dist simulator)
#
# Bidirectional: writes go to bc_out (Python→JS), reads come from the
# register file (JS→Python via _blinka.read_reg).
#
# Event format (broadcast on BroadcastChannel 'python-dist'):
#   {"type":"hw","cmd":"gpio_init",  "pin":"LED","direction":"output"}
#   {"type":"hw","cmd":"gpio_write", "pin":"LED","value":true}
#   {"type":"hw","cmd":"gpio_deinit","pin":"LED"}

import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))


class Direction:
    INPUT  = "input"
    OUTPUT = "output"


class Pull:
    UP   = "up"
    DOWN = "down"
    NONE = None


class DriveMode:
    PUSH_PULL  = "push_pull"
    OPEN_DRAIN = "open_drain"


class DigitalInOut:
    def __init__(self, pin):
        self._pin       = pin
        self._reg_addr  = _blinka.pin_to_reg(pin)
        self._direction = Direction.INPUT
        self._pull      = None

    def switch_to_output(self, value=False, drive_mode=DriveMode.PUSH_PULL):
        self._direction = Direction.OUTPUT
        self._pull      = None
        _hw({"type": "hw", "cmd": "gpio_init",
              "pin": self._pin, "direction": "output"})
        self.value = value

    def switch_to_input(self, pull=None):
        self._direction = Direction.INPUT
        self._pull      = pull
        _hw({"type": "hw", "cmd": "gpio_init",
              "pin": self._pin, "direction": "input",
              "pull": pull})

    @property
    def direction(self):
        return self._direction

    @direction.setter
    def direction(self, val):
        if val == Direction.OUTPUT:
            self.switch_to_output()
        else:
            self.switch_to_input()

    @property
    def value(self):
        # Read from hardware register file — reflects JS/DOM state
        if self._reg_addr >= 0:
            return bool(_blinka.read_reg(self._reg_addr))
        return False

    @value.setter
    def value(self, val):
        v = bool(val)
        # Write to register file (so subsequent reads see it)
        if self._reg_addr >= 0:
            _blinka.write_reg(self._reg_addr, 1 if v else 0)
        # Emit bc_out event (so JS/HardwareSimulator sees it)
        _hw({"type": "hw", "cmd": "gpio_write",
              "pin": self._pin, "value": v})

    @property
    def pull(self):
        return self._pull

    def deinit(self):
        _hw({"type": "hw", "cmd": "gpio_deinit", "pin": self._pin})

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()
