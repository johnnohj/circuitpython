/*
 * shims.js — Bundled CircuitPython compatibility shims for wasm-dist
 *
 * These are pure Python files that implement the standard CircuitPython API
 * by translating operations into bc_out hardware events (JSON lines on
 * BroadcastChannel 'python-dist' with type: "hw").
 *
 * Usage:
 *   import { BLINKA_SHIMS } from './shims.js';
 *   // Install all shims to /flash/lib/ in every executor worker:
 *   python.installBlinka();
 *   // Or install individually:
 *   python.writeModule('lib/board.py', BLINKA_SHIMS['board.py']);
 *
 * Python code then uses standard CircuitPython API:
 *   from digitalio import DigitalInOut, Direction
 *   import board
 *   led = DigitalInOut(board.LED)
 *   led.direction = Direction.OUTPUT
 *   led.value = True
 */

// ── board.py ─────────────────────────────────────────────────────────────────

const BOARD_PY = `\
# board.py — CircuitPython board pin constants (wasm-dist simulator)
LED  = "LED"
D0   = "D0";   D1   = "D1";   D2   = "D2";   D3   = "D3"
D4   = "D4";   D5   = "D5";   D6   = "D6";   D7   = "D7"
D8   = "D8";   D9   = "D9";   D10  = "D10";  D11  = "D11"
D12  = "D12";  D13  = "D13"
A0   = "A0";   A1   = "A1";   A2   = "A2";   A3   = "A3"
A4   = "A4";   A5   = "A5"
SCL  = "SCL";  SDA  = "SDA"
SCK  = "SCK";  MOSI = "MOSI"; MISO = "MISO"
TX   = "TX";   RX   = "RX"
`;

// ── digitalio.py ──────────────────────────────────────────────────────────────

const DIGITALIO_PY = `\
import _blinka
import json as _json
import struct as _struct

def _hw(msg):
    _blinka.send(_json.dumps(msg))

# Binary protocol: pack bp_gpio_t (pin u8, dir u8, pull u8, pad u8, value u16 LE)
_PULL_MAP = {"up": 1, "down": 2, None: 0}
def _hw_gpio_bin(sub, pin_addr, direction=0, pull=0, value=0):
    payload = _struct.pack('<BBBBH', pin_addr, direction, pull, 0, value)
    _blinka.send_bin(_blinka.BP_GPIO, sub, payload)

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
        if self._reg_addr >= 0:
            _hw_gpio_bin(_blinka.BP_INIT, self._reg_addr, direction=1)
        self.value = value

    def switch_to_input(self, pull=None):
        self._direction = Direction.INPUT
        self._pull      = pull
        _hw({"type": "hw", "cmd": "gpio_init",
              "pin": self._pin, "direction": "input",
              "pull": pull})
        if self._reg_addr >= 0:
            _hw_gpio_bin(_blinka.BP_INIT, self._reg_addr, direction=0,
                         pull=_PULL_MAP.get(pull, 0))

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
        if self._reg_addr >= 0:
            return bool(_blinka.read_reg(self._reg_addr))
        return False

    @value.setter
    def value(self, val):
        v = bool(val)
        if self._reg_addr >= 0:
            _blinka.write_reg(self._reg_addr, 1 if v else 0)
        _hw({"type": "hw", "cmd": "gpio_write",
              "pin": self._pin, "value": v})
        if self._reg_addr >= 0:
            _hw_gpio_bin(_blinka.BP_WRITE, self._reg_addr, value=1 if v else 0)

    @property
    def pull(self):
        return self._pull

    def deinit(self):
        _hw({"type": "hw", "cmd": "gpio_deinit", "pin": self._pin})
        if self._reg_addr >= 0:
            _hw_gpio_bin(_blinka.BP_DEINIT, self._reg_addr)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()
`;

// ── neopixel.py ───────────────────────────────────────────────────────────────

const NEOPIXEL_PY = `\
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
        if isinstance(idx, slice):
            for i, ci in zip(range(*idx.indices(self._n)), color):
                self._buf[i] = tuple(ci)
        else:
            self._buf[idx] = tuple(color)
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
`;

// ── displayio.py ──────────────────────────────────────────────────────────────

const DISPLAYIO_PY = `\
import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))

_next_id = 0
def _gen_id(prefix):
    global _next_id
    _next_id += 1
    return prefix + str(_next_id)

class Bitmap:
    def __init__(self, width, height, value_count):
        self.width = width
        self.height = height
        self.value_count = value_count
        self._buf = bytearray(width * height)
    def __setitem__(self, pos, value):
        x, y = pos
        if 0 <= x < self.width and 0 <= y < self.height:
            self._buf[y * self.width + x] = value
    def __getitem__(self, pos):
        x, y = pos
        return self._buf[y * self.width + x]
    def fill(self, value):
        for i in range(len(self._buf)):
            self._buf[i] = value

class Palette:
    def __init__(self, color_count):
        self._colors = [0] * color_count
        self._transparent = [False] * color_count
    def __len__(self):
        return len(self._colors)
    def __setitem__(self, idx, color):
        if isinstance(color, (tuple, list)):
            r, g, b = color[0], color[1], color[2]
            self._colors[idx] = (r << 16) | (g << 8) | b
        else:
            self._colors[idx] = color & 0xFFFFFF
    def __getitem__(self, idx):
        return self._colors[idx]
    def make_transparent(self, idx):
        self._transparent[idx] = True
    def make_opaque(self, idx):
        self._transparent[idx] = False
    def is_transparent(self, idx):
        return self._transparent[idx]

class TileGrid:
    def __init__(self, bitmap, pixel_shader, width=1, height=1,
                 tile_width=None, tile_height=None,
                 default_tile=0, x=0, y=0):
        self.bitmap = bitmap
        self.pixel_shader = pixel_shader
        self.x = x
        self.y = y
        self.flip_x = False
        self.flip_y = False
        self.transpose_xy = False
        self.tile_width = tile_width if tile_width else bitmap.width
        self.tile_height = tile_height if tile_height else bitmap.height
        self._width = width
        self._height = height
        self._tiles = [default_tile] * (width * height)
    def __getitem__(self, idx):
        if isinstance(idx, tuple):
            x, y = idx
            return self._tiles[y * self._width + x]
        return self._tiles[idx]
    def __setitem__(self, idx, value):
        if isinstance(idx, tuple):
            x, y = idx
            self._tiles[y * self._width + x] = value
        else:
            self._tiles[idx] = value
    @property
    def width(self):
        return self._width
    @property
    def height(self):
        return self._height

class Group:
    def __init__(self, scale=1, x=0, y=0):
        self._children = []
        self.scale = scale
        self.x = x
        self.y = y
    def append(self, child):
        self._children.append(child)
    def remove(self, child):
        self._children.remove(child)
    def insert(self, idx, child):
        self._children.insert(idx, child)
    def pop(self, idx=-1):
        return self._children.pop(idx)
    def __len__(self):
        return len(self._children)
    def __getitem__(self, idx):
        return self._children[idx]
    def __setitem__(self, idx, child):
        self._children[idx] = child
    def __contains__(self, child):
        return child in self._children

class Display:
    def __init__(self, bus=None, width=128, height=64,
                 auto_refresh=True, rotation=0):
        self._id = _gen_id("disp")
        self.width = width
        self.height = height
        self._auto_refresh = auto_refresh
        self._rotation = rotation
        self._root_group = None
        self._fb = bytearray(width * height * 3)
        self._fb_path = "/_fb_" + self._id
        _hw({"type": "hw", "cmd": "display_init",
             "id": self._id, "width": width, "height": height})
    @property
    def auto_refresh(self):
        return self._auto_refresh
    @auto_refresh.setter
    def auto_refresh(self, val):
        self._auto_refresh = val
    @property
    def rotation(self):
        return self._rotation
    @property
    def root_group(self):
        return self._root_group
    @root_group.setter
    def root_group(self, group):
        self._root_group = group
        if self._auto_refresh and group is not None:
            self.refresh()
    def refresh(self):
        fb = self._fb
        w = self.width
        h = self.height
        for i in range(len(fb)):
            fb[i] = 0
        if self._root_group is not None:
            self._composite_group(self._root_group, 0, 0, 1)
        f = open(self._fb_path, "wb")
        f.write(fb)
        f.close()
        _hw({"type": "hw", "cmd": "display_refresh",
             "id": self._id, "width": w, "height": h,
             "fb_path": self._fb_path})
    def _composite_group(self, group, ox, oy, scale):
        gx = ox + group.x * scale
        gy = oy + group.y * scale
        gs = scale * group.scale
        for child in group._children:
            if isinstance(child, Group):
                self._composite_group(child, gx, gy, gs)
            elif isinstance(child, TileGrid):
                self._composite_tilegrid(child, gx, gy, gs)
    def _composite_tilegrid(self, tg, ox, oy, scale):
        bm = tg.bitmap
        pal = tg.pixel_shader
        tw = tg.tile_width
        th = tg.tile_height
        fb = self._fb
        dw = self.width
        dh = self.height
        tiles_per_row = bm.width // tw if tw > 0 else 1
        for ty_idx in range(tg._height):
            for tx_idx in range(tg._width):
                tile = tg._tiles[ty_idx * tg._width + tx_idx]
                src_x0 = (tile % tiles_per_row) * tw
                src_y0 = (tile // tiles_per_row) * th
                dx0 = ox + (tg.x + tx_idx * tw) * scale
                dy0 = oy + (tg.y + ty_idx * th) * scale
                for py in range(th):
                    for px in range(tw):
                        bx = (tw - 1 - px) if tg.flip_x else px
                        by = (th - 1 - py) if tg.flip_y else py
                        idx = bm._buf[(src_y0 + by) * bm.width + (src_x0 + bx)]
                        if pal._transparent[idx]:
                            continue
                        color = pal._colors[idx]
                        r = (color >> 16) & 0xFF
                        g = (color >> 8) & 0xFF
                        b = color & 0xFF
                        for sy in range(scale):
                            fy = int(dy0 + py * scale + sy)
                            if fy < 0 or fy >= dh:
                                continue
                            for sx in range(scale):
                                fx = int(dx0 + px * scale + sx)
                                if fx < 0 or fx >= dw:
                                    continue
                                off = (fy * dw + fx) * 3
                                fb[off] = r
                                fb[off + 1] = g
                                fb[off + 2] = b
`;

// ── time.py ───────────────────────────────────────────────────────────────────

const TIME_PY = `\
import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))

def sleep(seconds):
    ms = int(seconds * 1000)
    if ms < 0:
        ms = 0
    _hw({"type": "hw", "cmd": "time_sleep", "ms": ms})
    _blinka.sync_registers()

_t0 = _blinka.ticks_ms()

def monotonic():
    return (_blinka.ticks_ms() - _t0) / 1000.0

def monotonic_ns():
    return (_blinka.ticks_ms() - _t0) * 1_000_000

def struct_time(tm):
    return tm

def localtime(secs=None):
    return (2026, 1, 1, 0, 0, 0, 0, 1)
`;

// ── pwmio.py ──────────────────────────────────────────────────────────────────

const PWMIO_PY = `\
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
`;

// ── analogio.py ───────────────────────────────────────────────────────────────

const ANALOGIO_PY = `\
import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))

class AnalogIn:
    def __init__(self, pin):
        self._pin = pin
        self._reg_addr = _blinka.pin_to_reg(pin)
        _hw({"type": "hw", "cmd": "analog_init",
             "pin": self._pin, "direction": "input"})
    @property
    def value(self):
        if self._reg_addr >= 0:
            return _blinka.read_reg(self._reg_addr)
        return 0
    @property
    def reference_voltage(self):
        return 3.3
    def deinit(self):
        _hw({"type": "hw", "cmd": "analog_deinit", "pin": self._pin})
    def __enter__(self):
        return self
    def __exit__(self, *args):
        self.deinit()

class AnalogOut:
    def __init__(self, pin):
        self._pin = pin
        self._reg_addr = _blinka.pin_to_reg(pin)
        self._value = 0
        _hw({"type": "hw", "cmd": "analog_init",
             "pin": self._pin, "direction": "output"})
    @property
    def value(self):
        return self._value
    @value.setter
    def value(self, val):
        self._value = val & 0xFFFF
        if self._reg_addr >= 0:
            _blinka.write_reg(self._reg_addr, self._value)
        _hw({"type": "hw", "cmd": "analog_write",
             "pin": self._pin, "value": self._value})
    def deinit(self):
        _hw({"type": "hw", "cmd": "analog_deinit", "pin": self._pin})
    def __enter__(self):
        return self
    def __exit__(self, *args):
        self.deinit()
`;

// ── busio.py ──────────────────────────────────────────────────────────────────

const BUSIO_PY = `\
import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))

_next_bus_id = 0
def _gen_bus_id(prefix):
    global _next_bus_id
    _next_bus_id += 1
    return prefix + str(_next_bus_id)

class I2C:
    def __init__(self, scl, sda, frequency=100000, timeout=255):
        self._id = _gen_bus_id("i2c")
        self._scl = scl
        self._sda = sda
        self._frequency = frequency
        self._locked = False
        _hw({"type": "hw", "cmd": "i2c_init",
             "id": self._id, "scl": scl, "sda": sda,
             "frequency": frequency})
    def deinit(self):
        _hw({"type": "hw", "cmd": "i2c_deinit", "id": self._id})
    def __enter__(self):
        return self
    def __exit__(self, *args):
        self.deinit()
    def try_lock(self):
        if self._locked:
            return False
        self._locked = True
        return True
    def unlock(self):
        self._locked = False
    def scan(self):
        _hw({"type": "hw", "cmd": "i2c_scan", "id": self._id})
        _blinka.sync_registers()
        try:
            f = open("/i2c_response", "r")
            data = f.read()
            f.close()
            if data:
                return _json.loads(data)
        except:
            pass
        return []
    def readfrom_into(self, address, buffer, start=0, end=None):
        if end is None:
            end = len(buffer)
        n = end - start
        _hw({"type": "hw", "cmd": "i2c_read",
             "id": self._id, "addr": address, "len": n})
        _blinka.sync_registers()
        try:
            f = open("/i2c_response", "rb")
            data = f.read()
            f.close()
            for i in range(min(n, len(data))):
                buffer[start + i] = data[i]
        except:
            pass
    def writeto(self, address, buffer, start=0, end=None):
        if end is None:
            end = len(buffer)
        data = list(buffer[start:end])
        _hw({"type": "hw", "cmd": "i2c_write",
             "id": self._id, "addr": address, "data": data})
    def writeto_then_readfrom(self, address, out_buffer, in_buffer,
                               out_start=0, out_end=None,
                               in_start=0, in_end=None):
        if out_end is None:
            out_end = len(out_buffer)
        if in_end is None:
            in_end = len(in_buffer)
        out_data = list(out_buffer[out_start:out_end])
        in_len = in_end - in_start
        _hw({"type": "hw", "cmd": "i2c_write_read",
             "id": self._id, "addr": address,
             "data": out_data, "read_len": in_len})
        _blinka.sync_registers()
        try:
            f = open("/i2c_response", "rb")
            data = f.read()
            f.close()
            for i in range(min(in_len, len(data))):
                in_buffer[in_start + i] = data[i]
        except:
            pass

class SPI:
    def __init__(self, clock, MOSI=None, MISO=None, half_duplex=False):
        self._id = _gen_bus_id("spi")
        self._clock = clock
        self._mosi = MOSI
        self._miso = MISO
        self._locked = False
        self._baudrate = 100000
        _hw({"type": "hw", "cmd": "spi_init",
             "id": self._id, "clock": clock,
             "mosi": MOSI, "miso": MISO})
    def deinit(self):
        _hw({"type": "hw", "cmd": "spi_deinit", "id": self._id})
    def __enter__(self):
        return self
    def __exit__(self, *args):
        self.deinit()
    def configure(self, baudrate=100000, polarity=0, phase=0, bits=8):
        self._baudrate = baudrate
        _hw({"type": "hw", "cmd": "spi_configure",
             "id": self._id, "baudrate": baudrate,
             "polarity": polarity, "phase": phase, "bits": bits})
    def try_lock(self):
        if self._locked:
            return False
        self._locked = True
        return True
    def unlock(self):
        self._locked = False
    @property
    def frequency(self):
        return self._baudrate
    def write(self, buffer, start=0, end=None):
        if end is None:
            end = len(buffer)
        _hw({"type": "hw", "cmd": "spi_write",
             "id": self._id, "data": list(buffer[start:end])})
    def readinto(self, buffer, start=0, end=None, write_value=0):
        if end is None:
            end = len(buffer)
        n = end - start
        _hw({"type": "hw", "cmd": "spi_read",
             "id": self._id, "len": n, "write_value": write_value})
        _blinka.sync_registers()
        try:
            f = open("/spi_response", "rb")
            data = f.read()
            f.close()
            for i in range(min(n, len(data))):
                buffer[start + i] = data[i]
        except:
            pass
    def write_readinto(self, out_buffer, in_buffer,
                       out_start=0, out_end=None,
                       in_start=0, in_end=None):
        if out_end is None:
            out_end = len(out_buffer)
        if in_end is None:
            in_end = len(in_buffer)
        in_len = in_end - in_start
        _hw({"type": "hw", "cmd": "spi_transfer",
             "id": self._id, "out": list(out_buffer[out_start:out_end]),
             "in_len": in_len})
        _blinka.sync_registers()
        try:
            f = open("/spi_response", "rb")
            data = f.read()
            f.close()
            for i in range(min(in_len, len(data))):
                in_buffer[in_start + i] = data[i]
        except:
            pass
`;

// ── Exports ───────────────────────────────────────────────────────────────────

/** Map of filename → Python source for all blinka shims. */
export const BLINKA_SHIMS = {
    'board.py':     BOARD_PY,
    'digitalio.py': DIGITALIO_PY,
    'neopixel.py':  NEOPIXEL_PY,
    'time.py':      TIME_PY,
    'displayio.py': DISPLAYIO_PY,
    'pwmio.py':     PWMIO_PY,
    'analogio.py':  ANALOGIO_PY,
    'busio.py':     BUSIO_PY,
};
