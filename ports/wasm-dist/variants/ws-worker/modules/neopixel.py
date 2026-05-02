# neopixel.py — WS protocol wrapper for NeoPixel.
#
# The upstream neopixel library is a frozen .mpy that uses
# neopixel_write underneath.  This wrapper intercepts the
# high-level NeoPixel class to broadcast pixel state changes
# via the WS protocol bus.
#
# Maps to Wippersnapper protobuf:
#   pixels.proto (PixelsCreateRequest, PixelsWriteRequest, PixelsDeleteRequest)

try:
    # The real neopixel module is frozen as adafruit_neopixel or
    # available as a built-in.  Import it under a private name.
    from neopixel import NeoPixel as _NeoPixel, GRB, GRBW, RGBW, RGB
except ImportError:
    # Fallback: build from primitives
    import adafruit_pixelbuf
    import neopixel_write
    import digitalio

    GRB = 'GRB'
    GRBW = 'GRBW'
    RGB = 'RGB'
    RGBW = 'RGBW'

    class _NeoPixel(adafruit_pixelbuf.PixelBuf):
        def __init__(self, pin, n, *, bpp=3, brightness=1.0,
                     auto_write=True, pixel_order=None):
            if not pixel_order:
                pixel_order = GRB if bpp == 3 else GRBW
            super().__init__(n, brightness=brightness,
                             byteorder=pixel_order, auto_write=auto_write)
            self._pin = digitalio.DigitalInOut(pin)
            self._pin.direction = digitalio.Direction.OUTPUT

        def deinit(self):
            self._pin.deinit()

        def __enter__(self):
            return self

        def __exit__(self, *args):
            self.deinit()

        def _transmit(self, buf):
            neopixel_write.neopixel_write(self._pin, buf)

import _ws_bus


class NeoPixel(_NeoPixel):
    """NeoPixel with WS protocol broadcasting."""

    def __init__(self, pin, n, *, bpp=3, brightness=1.0,
                 auto_write=True, pixel_order=None):
        super().__init__(pin, n, bpp=bpp, brightness=brightness,
                         auto_write=auto_write, pixel_order=pixel_order)
        self._ws_pin_name = str(pin).split('.')[-1].rstrip(')')
        self._ws_pin_id = pin.id if hasattr(pin, 'id') else -1
        self._ws_count = n
        _ws_bus.emit('pixels_create',
            pin_name=self._ws_pin_name,
            pin_id=self._ws_pin_id,
            pixel_type='NEOPIXEL',
            pixel_count=n,
            pixel_order=str(pixel_order or (GRB if bpp == 3 else GRBW)),
            brightness=brightness)

    def _transmit(self, buf):
        super()._transmit(buf)
        # Broadcast packed pixel data as list of 32-bit WRGB values
        colors = []
        bpp = len(buf) // self._ws_count if self._ws_count else 3
        for i in range(self._ws_count):
            off = i * bpp
            if bpp == 4:
                colors.append(
                    (buf[off] << 24) | (buf[off+1] << 16) |
                    (buf[off+2] << 8) | buf[off+3])
            else:
                colors.append(
                    (buf[off] << 16) | (buf[off+1] << 8) | buf[off+2])
        _ws_bus.emit('pixels_write',
            pin_name=self._ws_pin_name,
            pixel_colors=colors)

    def deinit(self):
        _ws_bus.emit('pixels_delete',
            pin_name=self._ws_pin_name)
        super().deinit()
