# neopixel.py — CircuitPython NeoPixel shim (wasm-dist simulator)
# Translates NeoPixel operations to bc_out hardware events.
#
# Event format:
#   {"type":"hw","cmd":"neo_init",  "pin":"D10","n":10,"order":"GRB"}
#   {"type":"hw","cmd":"neo_write", "pin":"D10","pixels":[[r,g,b],...]}
#   {"type":"hw","cmd":"neo_deinit","pin":"D10"}

import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))


RGB  = "RGB"
GRB  = "GRB"
RGBW = "RGBW"
GRBW = "GRBW"


class NeoPixel:
    def __init__(self, pin, n, brightness=1.0, auto_write=True,
                 pixel_order=GRB, bpp=None):
        self._pin        = pin
        self._n          = n
        self._brightness = max(0.0, min(1.0, brightness))
        self._auto_write = auto_write
        self._order      = pixel_order
        self._bpp        = len(pixel_order) if bpp is None else bpp
        self._buf        = [(0,) * self._bpp] * n
        _hw({"type": "hw", "cmd": "neo_init",
              "pin": pin, "n": n, "order": pixel_order})

    def __len__(self):
        return self._n

    def __setitem__(self, idx, color):
        c = tuple(color)
        if isinstance(idx, slice):
            idxs = range(*idx.indices(self._n))
            for i, ci in zip(idxs, color):
                self._buf[i] = tuple(ci)
        else:
            self._buf[idx] = c
        if self._auto_write:
            self.show()

    def __getitem__(self, idx):
        return self._buf[idx]

    @property
    def brightness(self):
        return self._brightness

    @brightness.setter
    def brightness(self, val):
        self._brightness = max(0.0, min(1.0, val))
        if self._auto_write:
            self.show()

    def fill(self, color):
        self._buf = [tuple(color)] * self._n
        if self._auto_write:
            self.show()

    def show(self):
        b = self._brightness
        pixels = [tuple(int(c * b) for c in p) for p in self._buf]
        _hw({"type": "hw", "cmd": "neo_write",
              "pin": self._pin, "pixels": pixels})

    def deinit(self):
        _hw({"type": "hw", "cmd": "neo_deinit", "pin": self._pin})

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()
