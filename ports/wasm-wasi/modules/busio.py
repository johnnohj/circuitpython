# busio.py -- CircuitPython busio shim for WASI reactor
#
# I2C: reads/writes device register files at /hw/i2c/dev/{addr}
# SPI: transfers through /hw/spi/xfer
# UART: TX appends to /hw/uart/{port}/tx, RX reads /hw/uart/{port}/rx
#
# These match the C common-hal wire formats exactly.

import struct
import os


def _ensure(*dirs):
    for d in dirs:
        try:
            os.mkdir(d)
        except OSError:
            pass


# ── I2C ──────────────────────────────────────────────────────────────

class I2C:
    """Virtual I2C backed by OPFS register files.

    Each device address maps to /hw/i2c/dev/{addr}.
    Write [reg, data...] seeks to reg offset and writes data.
    Read returns bytes from the current position.
    """

    def __init__(self, scl, sda, frequency=100000):
        self._scl = scl
        self._sda = sda
        self._frequency = frequency
        self._locked = False
        _ensure("/hw", "/hw/i2c", "/hw/i2c/dev")

    def deinit(self):
        self._locked = False

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
        """Return list of addresses that have device files."""
        addrs = []
        try:
            for name in os.listdir("/hw/i2c/dev"):
                try:
                    addrs.append(int(name))
                except ValueError:
                    pass
        except OSError:
            pass
        addrs.sort()
        return addrs

    def _dev_path(self, addr):
        return "/hw/i2c/dev/" + str(addr)

    def writeto(self, address, buffer, *, start=0, end=None):
        if end is None:
            end = len(buffer)
        data = buffer[start:end]
        if len(data) == 0:
            return
        path = self._dev_path(address)
        reg = data[0]
        try:
            f = open(path, "r+b")
        except OSError:
            # Create the device file
            f = open(path, "wb")
            f.write(b'\xff' * 256)
            f.close()
            f = open(path, "r+b")
        f.seek(reg)
        if len(data) > 1:
            f.write(data[1:])
        f.close()

    def readfrom_into(self, address, buffer, *, start=0, end=None):
        if end is None:
            end = len(buffer)
        length = end - start
        path = self._dev_path(address)
        try:
            f = open(path, "rb")
            data = f.read(256)
            f.close()
            # Read from offset 0 by default (register pointer set by prior write)
            for i in range(length):
                if i < len(data):
                    buffer[start + i] = data[i]
                else:
                    buffer[start + i] = 0xFF
        except OSError:
            for i in range(length):
                buffer[start + i] = 0xFF

    def writeto_then_readfrom(self, address, out_buffer, in_buffer,
                               *, out_start=0, out_end=None,
                               in_start=0, in_end=None):
        if out_end is None:
            out_end = len(out_buffer)
        if in_end is None:
            in_end = len(in_buffer)

        out_data = out_buffer[out_start:out_end]
        in_len = in_end - in_start
        path = self._dev_path(address)

        try:
            f = open(path, "r+b")
        except OSError:
            f = open(path, "wb")
            f.write(b'\xff' * 256)
            f.close()
            f = open(path, "r+b")

        # Write phase: first byte is register address
        if len(out_data) > 0:
            reg = out_data[0]
            f.seek(reg)
            if len(out_data) > 1:
                f.write(out_data[1:])
            # Re-seek to register for read phase
            f.seek(reg)

        # Read phase
        data = f.read(in_len)
        f.close()
        for i in range(in_len):
            if i < len(data):
                in_buffer[in_start + i] = data[i]
            else:
                in_buffer[in_start + i] = 0xFF


# ── SPI ──────────────────────────────────────────────────────────────

class SPI:
    """Virtual SPI backed by /hw/spi/xfer transfer file."""

    def __init__(self, clock, MOSI=None, MISO=None, half_duplex=False):
        self._clock = clock
        self._mosi = MOSI
        self._miso = MISO
        self._locked = False
        self._baudrate = 1000000
        self._polarity = 0
        self._phase = 0
        self._bits = 8
        _ensure("/hw", "/hw/spi")

    def deinit(self):
        self._locked = False

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    def configure(self, baudrate=1000000, polarity=0, phase=0, bits=8):
        self._baudrate = baudrate
        self._polarity = polarity
        self._phase = phase
        self._bits = bits

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

    def write(self, buf, *, start=0, end=None):
        if end is None:
            end = len(buf)
        try:
            f = open("/hw/spi/xfer", "wb")
            f.write(buf[start:end])
            f.close()
        except OSError:
            pass

    def readinto(self, buf, *, start=0, end=None, write_value=0):
        if end is None:
            end = len(buf)
        length = end - start
        try:
            f = open("/hw/spi/xfer", "rb")
            data = f.read(length)
            f.close()
            for i in range(length):
                if i < len(data):
                    buf[start + i] = data[i]
                else:
                    buf[start + i] = write_value
        except OSError:
            for i in range(length):
                buf[start + i] = write_value

    def write_readinto(self, out_buf, in_buf, *,
                        out_start=0, out_end=None,
                        in_start=0, in_end=None):
        if out_end is None:
            out_end = len(out_buf)
        if in_end is None:
            in_end = len(in_buf)
        self.write(out_buf, start=out_start, end=out_end)
        self.readinto(in_buf, start=in_start, end=in_end)


# ── UART ─────────────────────────────────────────────────────────────

class UART:
    """Virtual UART backed by /hw/uart/{port}/rx and tx files."""

    _next_port = 0

    def __init__(self, tx=None, rx=None, *, baudrate=9600, bits=8,
                 parity=None, stop=1, timeout=1.0,
                 receiver_buffer_size=64):
        self._tx_pin = tx
        self._rx_pin = rx
        self._baudrate = baudrate
        self._timeout = timeout
        self._port = UART._next_port
        UART._next_port += 1
        _ensure("/hw", "/hw/uart", "/hw/uart/" + str(self._port))
        # Create empty endpoint files
        for ep in ("rx", "tx"):
            path = "/hw/uart/{}/{}".format(self._port, ep)
            try:
                f = open(path, "wb")
                f.close()
            except OSError:
                pass

    def deinit(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    def read(self, nbytes=None):
        path = "/hw/uart/{}/rx".format(self._port)
        try:
            f = open(path, "rb")
            data = f.read(nbytes) if nbytes else f.read()
            f.close()
            if data:
                # Consume: truncate the file
                f = open(path, "wb")
                f.close()
                return data
        except OSError:
            pass
        return None

    def readline(self):
        path = "/hw/uart/{}/rx".format(self._port)
        try:
            f = open(path, "rb")
            data = f.read()
            f.close()
            if data:
                nl = data.find(b'\n')
                if nl >= 0:
                    line = data[:nl + 1]
                    # Write back remainder
                    remainder = data[nl + 1:]
                    f = open(path, "wb")
                    f.write(remainder)
                    f.close()
                    return line
                # No newline — return all and consume
                f = open(path, "wb")
                f.close()
                return data
        except OSError:
            pass
        return None

    def write(self, buf):
        path = "/hw/uart/{}/tx".format(self._port)
        try:
            f = open(path, "ab")
            f.write(buf)
            f.close()
            return len(buf)
        except OSError:
            return 0

    @property
    def in_waiting(self):
        path = "/hw/uart/{}/rx".format(self._port)
        try:
            st = os.stat(path)
            return st[6]  # st_size
        except OSError:
            return 0

    @property
    def baudrate(self):
        return self._baudrate

    @baudrate.setter
    def baudrate(self, val):
        self._baudrate = val

    @property
    def timeout(self):
        return self._timeout

    @timeout.setter
    def timeout(self, val):
        self._timeout = val

    def reset_input_buffer(self):
        path = "/hw/uart/{}/rx".format(self._port)
        try:
            f = open(path, "wb")
            f.close()
        except OSError:
            pass
