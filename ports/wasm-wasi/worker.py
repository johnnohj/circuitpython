"""
worker.py -- Hardware peripheral controller for CircuitPython WASI port.

Runs on a dedicated Web Worker with its own CircuitPython/WASM instance.
Monitors OPFS endpoints written by the main-thread reactor and drives
display compositing, REPL terminal rendering, and peripheral state.

Architecture:
    Main thread reactor           OPFS "DMA bus"              This worker
    ==================           ==============              ===========
    User code.py                                             worker.py
      import displayio  ──write──▶ /hw/display/scene         poll + composite
      import digitalio  ──write──▶ /hw/gpio/state            poll + reflect
      import neopixel   ──write──▶ /hw/neopixel/data         poll + reflect
      print("hello")    ──write──▶ /hw/repl/tx               poll + render
                                                              ──write──▶ /hw/display/fb
                                                              (JS reads fb, paints Canvas)

    JS host writes:
      keyboard events   ──write──▶ /hw/repl/rx               worker reads → reactor stdin
      canvas resize     ──write──▶ /hw/events/resize
      mouse/focus       ──write──▶ /hw/events/input

    Reactor signals:
      sys signals       ──write──▶ /hw/control               worker polls + dispatches

OPFS Endpoint Layout:
    /hw/
      control               Signal + metadata (fixed-size struct)
      display/
        scene               User display content: RGB888 bytes from displayio shim
        fb                  Final composited framebuffer (worker writes, JS reads)
        meta                Display metadata JSON: {width, height, rotation, dirty}
      repl/
        tx                  REPL text output (reactor writes, worker reads + renders)
        rx                  Keyboard input (JS writes, worker reads → forwards to reactor)
      gpio/
        state               Pin state table (reactor shim writes on gpio_write)
      neopixel/
        data                Pixel buffer (reactor shim writes on show())
      i2c/
        request             Transaction request (reactor shim writes)
        response            Transaction response (worker writes after driver processes)
      spi/
        request             SPI transaction request
        response            SPI transaction response
      events/
        resize              Canvas dimensions from JS
        input               Mouse, focus, touch events from JS
"""

import os
import struct
import time

# ---------------------------------------------------------------------------
# Signal protocol (reactor -> worker via /hw/control byte 0)
# ---------------------------------------------------------------------------
# The reactor (or JS host) writes a nonzero signal byte.  The worker reads it
# in its main loop and clears it back to zero after handling.  One signal at a
# time; the sender must wait for acknowledgement (byte == 0) before sending
# another.  For high-frequency events, use dedicated endpoints instead.

SIG_NONE    = 0x00
SIG_REFRESH = 0x01  # User called display.refresh() -- recomposite
SIG_RESIZE  = 0x02  # Canvas dimensions changed (read /hw/events/resize)
SIG_INT     = 0x03  # Ctrl-C: forward KeyboardInterrupt to REPL
SIG_HUP     = 0x04  # Reload config / re-discover endpoints
SIG_TERM    = 0x05  # Clean shutdown
SIG_USR1    = 0x06  # Dump diagnostics to /hw/events/diag
SIG_USR2    = 0x07  # Reserved

# ---------------------------------------------------------------------------
# Control file layout (/hw/control)  -- 32 bytes, fixed
# ---------------------------------------------------------------------------
#   Offset  Size  Field
#   0       1     signal      (pending signal byte, 0 = none)
#   1       1     state       (worker lifecycle state)
#   2       2     canvas_w    (current canvas width, JS-writable)
#   4       2     canvas_h    (current canvas height, JS-writable)
#   6       2     _reserved
#   8       4     frame_cnt   (worker increments per composite, JS reads for
#                              dirty-check -- same pattern as wasm_display_frame_count)
#   12      4     tick_ms     (worker monotonic ms, useful for latency measurement)
#   16      16    _pad        (future: endpoint bitmap, error code, etc.)

CTRL_FMT  = '<BB HH H I I 16s'
CTRL_SIZE = 32  # struct.calcsize(CTRL_FMT) == 32

# Worker lifecycle states (control byte 1)
WS_INIT     = 0
WS_RUNNING  = 1
WS_STOPPING = 2
WS_STOPPED  = 3

# ---------------------------------------------------------------------------
# Endpoint monitor
# ---------------------------------------------------------------------------

class Endpoint:
    """Thin wrapper around an OPFS-backed file for polling and I/O."""

    __slots__ = ('path', '_mtime')

    def __init__(self, path):
        self.path = path
        self._mtime = 0

    def exists(self):
        try:
            os.stat(self.path)
            return True
        except OSError:
            return False

    def changed(self):
        """True if the file was modified since we last checked."""
        try:
            mt = os.stat(self.path)[8]  # st_mtime
            if mt != self._mtime:
                self._mtime = mt
                return True
        except OSError:
            pass
        return False

    def read(self):
        """Read entire file as bytes, or None on error."""
        try:
            with open(self.path, 'rb') as f:
                return f.read()
        except OSError:
            return None

    def write(self, data):
        """Write bytes (creates or overwrites)."""
        with open(self.path, 'wb') as f:
            f.write(data)

    def read_into(self, buf, offset=0):
        """Read into a pre-allocated buffer.  Returns bytes read."""
        raw = self.read()
        if raw is None:
            return 0
        n = min(len(raw), len(buf) - offset)
        buf[offset:offset + n] = raw[:n]
        return n

# ---------------------------------------------------------------------------
# Peripheral handlers
# ---------------------------------------------------------------------------
# Each handler owns one or more endpoints.  The worker calls poll() on each
# handler every tick; the handler decides whether anything needs doing.

class DisplayHandler:
    """Reads user scene data + REPL text, composites layers, writes final fb.

    Layer stack (bottom to top):
      0  Background fill (configurable color, default black)
      1  User scene -- RGB888 blob from /hw/display/scene
      2  REPL terminal overlay -- rendered by this handler from /hw/repl/tx
      3  Status bar (optional: connection state, exceptions, etc.)

    The final composited framebuffer is written to /hw/display/fb as raw
    RGB888 bytes.  JS polls frame_cnt in /hw/control to detect changes.
    """

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self._fb = bytearray(width * height * 3)
        self._dirty = True
        self._scene = Endpoint('/hw/display/scene')
        self._fb_out = Endpoint('/hw/display/fb')
        # REPL terminal state
        self._repl_tx = Endpoint('/hw/repl/tx')
        self._repl_lines = []  # ring of recent lines for terminal rendering

    def poll(self):
        changed = False
        if self._scene.changed():
            self._dirty = True
            changed = True
        if self._repl_tx.changed():
            self._ingest_repl_text()
            self._dirty = True
            changed = True
        if self._dirty:
            self._composite()
            self._dirty = False
        return changed

    def resize(self, width, height):
        if width == self.width and height == self.height:
            return
        self.width = width
        self.height = height
        self._fb = bytearray(width * height * 3)
        self._dirty = True

    def _ingest_repl_text(self):
        """Read new REPL output and append to terminal line buffer."""
        data = self._repl_tx.read()
        if not data:
            return
        # TODO: VT100 escape sequence parsing, cursor tracking
        # For now, split on newlines and keep a scrollback window
        text = data.decode('utf-8', 'replace')
        for line in text.split('\n'):
            self._repl_lines.append(line)
        # Cap scrollback to ~terminal height
        max_lines = self.height // 8  # assuming 8px font height
        if len(self._repl_lines) > max_lines * 2:
            self._repl_lines = self._repl_lines[-max_lines:]

    def _composite(self):
        """Merge layers into the output framebuffer."""
        fb = self._fb
        w = self.width

        # Layer 0: clear to black
        for i in range(len(fb)):
            fb[i] = 0

        # Layer 1: user scene (raw RGB888 blit)
        scene_data = self._scene.read()
        if scene_data:
            n = min(len(scene_data), len(fb))
            fb[:n] = scene_data[:n]

        # Layer 2: REPL terminal overlay
        # TODO: render self._repl_lines using frozen terminalio/font
        # For now, the REPL text is available but not yet rendered to pixels.
        # This is where we'd use displayio.TileGrid + fontio.BuiltinFont
        # to render terminal text over the user scene.

        # Layer 3: status bar
        # TODO: connection indicator, error state, etc.

        # Emit
        self._fb_out.write(fb)


class REPLHandler:
    """Bidirectional REPL I/O relay.

    rx: JS keyboard -> /hw/repl/rx -> worker reads -> forward to reactor stdin
    tx: reactor stdout -> /hw/repl/tx -> worker reads -> DisplayHandler renders

    The tx path is consumed by DisplayHandler.  This handler owns the rx
    (input) path: reading keystrokes and forwarding them to the reactor.
    """

    def __init__(self):
        self._rx = Endpoint('/hw/repl/rx')
        # Where we forward input to the reactor.  Could be:
        # - Another OPFS file the reactor polls (/hw/repl/stdin_fwd)
        # - A signal + shared buffer
        # - Direct postMessage via JS bridge
        self._reactor_stdin = Endpoint('/hw/repl/stdin_fwd')

    def poll(self):
        if self._rx.changed():
            data = self._rx.read()
            if data:
                self._forward_input(data)
            return True
        return False

    def _forward_input(self, data):
        """Forward keyboard input to the reactor's stdin."""
        # Check for Ctrl-C (0x03) -- escalate to signal
        if b'\x03' in data:
            # Write SIG_INT to control file
            try:
                with open('/hw/control', 'r+b') as f:
                    f.write(bytes([SIG_INT]))
            except OSError:
                pass
            # Strip the Ctrl-C and forward the rest
            data = data.replace(b'\x03', b'')
        if data:
            self._reactor_stdin.write(data)


class GPIOHandler:
    """Monitors GPIO pin state written by the reactor's digitalio shim.

    The pin state file is a flat array of uint16 values (one per pin),
    same layout as the OPFS_REGION_REGISTERS concept.
    """

    def __init__(self):
        self._state = Endpoint('/hw/gpio/state')

    def poll(self):
        return self._state.changed()
        # TODO: when changed, optionally reflect state to simulated
        # peripherals (LEDs, motor drivers, etc.)


class NeoPixelHandler:
    """Monitors NeoPixel data written by the reactor's neopixel shim.

    Data format: 4 bytes header (uint16 count, uint8 bpp, uint8 brightness)
    followed by count * bpp bytes of pixel data.
    """

    def __init__(self):
        self._data = Endpoint('/hw/neopixel/data')

    def poll(self):
        return self._data.changed()
        # TODO: read pixel data, optionally render a visual representation
        # in the display overlay, or write to a separate OPFS file for
        # JS-side NeoPixel visualization


class BusHandler:
    """Handles I2C or SPI transaction mailboxes.

    The reactor's busio shim writes a transaction request to the request
    endpoint.  This handler reads it, routes it to the appropriate
    simulated sensor driver (e.g., adafruit_bmp280 reading from OPFS-backed
    register files), and writes the response.

    This is where real Adafruit drivers run against simulated hardware:
    the worker instantiates the driver, and the driver's I2C reads resolve
    to data written by the test harness or UI.
    """

    def __init__(self, bus_type):
        self.bus_type = bus_type  # 'i2c' or 'spi'
        self._request = Endpoint(f'/hw/{bus_type}/request')
        self._response = Endpoint(f'/hw/{bus_type}/response')
        self._drivers = {}  # addr -> driver instance

    def poll(self):
        if self._request.changed():
            req = self._request.read()
            if req:
                resp = self._dispatch(req)
                if resp is not None:
                    self._response.write(resp)
            return True
        return False

    def register_driver(self, addr, driver):
        """Register a simulated sensor driver at an I2C/SPI address."""
        self._drivers[addr] = driver

    def _dispatch(self, request):
        """Route a transaction to the appropriate driver.

        Request format TBD -- likely mirrors the busio shim's JSON events
        or a compact binary protocol.
        """
        # TODO: parse request, extract address, route to self._drivers[addr]
        return None


# ---------------------------------------------------------------------------
# Endpoint discovery
# ---------------------------------------------------------------------------

def discover_handlers():
    """Probe /hw/ for present endpoints and instantiate handlers.

    Called at startup and on SIG_HUP (hot-reload).  Only creates handlers
    for endpoints that exist -- the reactor's mpconfigboard.h determines
    which shims are frozen in, and therefore which endpoints get created.
    """
    handlers = {}

    # Display is always present (it's the whole point of the worker)
    handlers['display'] = None  # sized after we read canvas dimensions

    # REPL relay
    handlers['repl'] = REPLHandler()

    # Probe optional peripherals
    for name, factory in [
        ('gpio',     lambda: GPIOHandler()),
        ('neopixel', lambda: NeoPixelHandler()),
    ]:
        if _dir_exists(f'/hw/{name}'):
            handlers[name] = factory()

    for bus_type in ('i2c', 'spi'):
        if _dir_exists(f'/hw/{bus_type}'):
            handlers[bus_type] = BusHandler(bus_type)

    return handlers


def _dir_exists(path):
    try:
        return (os.stat(path)[0] & 0o170000) == 0o040000  # S_IFDIR
    except OSError:
        return False


# ---------------------------------------------------------------------------
# Control file I/O
# ---------------------------------------------------------------------------

def read_control():
    """Read /hw/control -> (signal, state, canvas_w, canvas_h, frame_cnt, tick_ms)"""
    try:
        with open('/hw/control', 'rb') as f:
            data = f.read(CTRL_SIZE)
        if len(data) >= 16:
            sig, st, cw, ch, _, fcnt, tms = struct.unpack('<BBHHHII', data[:16])
            return sig, st, cw, ch, fcnt, tms
    except OSError:
        pass
    return SIG_NONE, WS_INIT, 0, 0, 0, 0


def write_control(signal=SIG_NONE, state=WS_INIT, canvas_w=0, canvas_h=0,
                  frame_cnt=0, tick_ms=0):
    """Write /hw/control with current worker state."""
    pad = b'\x00' * 16
    data = struct.pack('<BBHHHII', signal, state, canvas_w, canvas_h,
                       0, frame_cnt, tick_ms) + pad
    with open('/hw/control', 'wb') as f:
        f.write(data)


def ack_signal():
    """Clear the pending signal (byte 0) without disturbing other fields."""
    try:
        with open('/hw/control', 'r+b') as f:
            f.write(b'\x00')
    except OSError:
        pass


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main():
    print("worker: init")

    # --- Bootstrap: read canvas size from control file ---
    _, _, canvas_w, canvas_h, _, _ = read_control()
    if canvas_w == 0:
        canvas_w = 320
    if canvas_h == 0:
        canvas_h = 240

    # --- Discover endpoints ---
    handlers = discover_handlers()
    handlers['display'] = DisplayHandler(canvas_w, canvas_h)

    # --- Signal ready ---
    frame_cnt = 0
    write_control(SIG_NONE, WS_RUNNING, canvas_w, canvas_h, 0, 0)

    ep_names = sorted(handlers.keys())
    print(f"worker: ready {canvas_w}x{canvas_h}, endpoints: {ep_names}")

    # --- Run ---
    running = True

    while running:
        # 1. Check signals
        sig, _, cw, ch, _, _ = read_control()

        if sig != SIG_NONE:
            if sig == SIG_TERM:
                print("worker: SIGTERM")
                running = False
                ack_signal()
                continue

            elif sig == SIG_RESIZE:
                canvas_w, canvas_h = cw, ch
                handlers['display'].resize(canvas_w, canvas_h)

            elif sig == SIG_INT:
                # Ctrl-C is also handled inline by REPLHandler when it
                # sees 0x03 in the input stream.  This signal path covers
                # the case where JS sends it directly (e.g., toolbar button).
                pass

            elif sig == SIG_HUP:
                # Re-discover endpoints (e.g., user plugged in a new sensor)
                display = handlers.get('display')
                handlers = discover_handlers()
                handlers['display'] = display or DisplayHandler(canvas_w, canvas_h)
                print(f"worker: rehup, endpoints: {sorted(handlers.keys())}")

            elif sig == SIG_REFRESH:
                # Explicit refresh request -- display handler will pick up
                # the scene change on its next poll, but we can force it
                if 'display' in handlers:
                    handlers['display']._dirty = True

            elif sig == SIG_USR1:
                # Diagnostics dump
                print(f"worker: diag frame={frame_cnt} endpoints={sorted(handlers.keys())}")

            ack_signal()

        # 2. Poll all handlers
        for handler in handlers.values():
            if handler is not None:
                if handler.poll():
                    if isinstance(handler, DisplayHandler):
                        frame_cnt += 1

        # 3. Update control file (state + frame counter for JS dirty-check)
        tick_ms = int(time.monotonic() * 1000) & 0xFFFFFFFF
        write_control(SIG_NONE, WS_RUNNING, canvas_w, canvas_h,
                      frame_cnt, tick_ms)

        # 4. Yield -- don't spin at 100%.
        # In the browser, time.sleep() yields to the JS event loop via
        # the WASI poll_oneoff syscall.  1ms gives us up to 1000Hz polling
        # which is well above the 60Hz display refresh.
        time.sleep(0.001)

    # --- Shutdown ---
    write_control(SIG_NONE, WS_STOPPED, canvas_w, canvas_h,
                  frame_cnt, int(time.monotonic() * 1000) & 0xFFFFFFFF)
    print("worker: stopped")


if __name__ == '__main__':
    main()
