# displayio.py — CircuitPython displayio shim (wasm-dist simulator)
#
# Implements the core displayio API by compositing in Python and sending
# the framebuffer to JS via bc_out for Canvas2D rendering.
#
# Supported classes: Bitmap, Palette, TileGrid, Group, Display
#
# Architecture:
#   Display.refresh() composites the scene graph into an RGB bytearray,
#   writes it to a MEMFS file, and sends a small bc_out signal.
#   The JS drain reads the binary and broadcasts it as a Uint8Array.

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
    """2D array of palette indices."""

    def __init__(self, width, height, value_count):
        self.width = width
        self.height = height
        self.value_count = value_count
        self._buf = bytearray(width * height)

    def __setitem__(self, pos, value):
        if isinstance(pos, tuple):
            x, y = pos
        else:
            raise TypeError("index must be (x, y) tuple")
        if 0 <= x < self.width and 0 <= y < self.height:
            self._buf[y * self.width + x] = value

    def __getitem__(self, pos):
        if isinstance(pos, tuple):
            x, y = pos
        else:
            raise TypeError("index must be (x, y) tuple")
        return self._buf[y * self.width + x]

    def fill(self, value):
        for i in range(len(self._buf)):
            self._buf[i] = value


class Palette:
    """Color lookup table mapping indices to 24-bit RGB colors."""

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
    """Maps a region of a Bitmap onto a display area."""

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

    @property
    def pixel_width(self):
        return self._width * self.tile_width

    @property
    def pixel_height(self):
        return self._height * self.tile_height


class Group:
    """Ordered collection of TileGrids and sub-Groups."""

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

    def index(self, child):
        return self._children.index(child)

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
    """Composites a scene graph into an RGB framebuffer and sends it to JS.

    The ``bus`` parameter is ignored (no real SPI/I2C in the simulator).
    Set ``root_group`` to a Group to display content; call ``refresh()``
    or rely on ``auto_refresh=True`` (which refreshes on root_group set).
    """

    def __init__(self, bus=None, width=128, height=64,
                 auto_refresh=True, rotation=0):
        self._id = _gen_id("disp")
        self.width = width
        self.height = height
        self._auto_refresh = auto_refresh
        self._rotation = rotation
        self._root_group = None
        # RGB888 framebuffer: 3 bytes per pixel
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
        """Composite the scene graph into the framebuffer and send to JS."""
        fb = self._fb
        w = self.width
        h = self.height
        # Clear to black
        for i in range(len(fb)):
            fb[i] = 0
        # Composite
        if self._root_group is not None:
            self._composite_group(self._root_group, 0, 0, 1)
        # Write raw RGB bytes to MEMFS file (VFS-accessible path)
        f = open(self._fb_path, "wb")
        f.write(fb)
        f.close()
        # Signal JS — the drain will read the binary and attach it
        _hw({"type": "hw", "cmd": "display_refresh",
             "id": self._id, "width": w, "height": h,
             "fb_path": self._fb_path})

    # ── Compositing engine ─────────────────────────────────────────────

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
                        # Flip
                        bx = (tw - 1 - px) if tg.flip_x else px
                        by = (th - 1 - py) if tg.flip_y else py

                        idx = bm._buf[(src_y0 + by) * bm.width + (src_x0 + bx)]

                        if pal._transparent[idx]:
                            continue

                        color = pal._colors[idx]
                        r = (color >> 16) & 0xFF
                        g = (color >> 8) & 0xFF
                        b = color & 0xFF

                        # Scale: fill a scale×scale block
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
