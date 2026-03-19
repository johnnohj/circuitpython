# busio.py — CircuitPython bus I/O shim (wasm-dist simulator)
#
# I2C and SPI transactions are serialized as bc_out events.  For reads,
# the response data comes from a MEMFS "mailbox" file that JS-side device
# simulators populate before the Python code reads it.
#
# The pattern mirrors real hardware: Python initiates a transaction (writes
# address + command bytes), the peripheral processes it (JS simulator fills
# response), Python reads the result (from the mailbox).
#
# Mailbox files:
#   /i2c_response   — JS writes response bytes here before Python reads
#   /spi_response   — same for SPI
#
# Event format (bc_out → BroadcastChannel):
#   {"type":"hw","cmd":"i2c_init",    "scl":"SCL","sda":"SDA","frequency":100000}
#   {"type":"hw","cmd":"i2c_scan",    "id":"i2c0"}
#   {"type":"hw","cmd":"i2c_write",   "id":"i2c0","addr":60,"data":[0,1,2]}
#   {"type":"hw","cmd":"i2c_read",    "id":"i2c0","addr":60,"len":4}
#   {"type":"hw","cmd":"i2c_deinit",  "id":"i2c0"}
#   {"type":"hw","cmd":"spi_init",    "clock":"SCK","mosi":"MOSI","miso":"MISO"}
#   {"type":"hw","cmd":"spi_configure","id":"spi0","baudrate":1000000,...}
#   {"type":"hw","cmd":"spi_write",   "id":"spi0","data":[...]}
#   {"type":"hw","cmd":"spi_read",    "id":"spi0","len":N,"write_value":0}
#   {"type":"hw","cmd":"spi_transfer","id":"spi0","out":[...],"in_len":N}
#   {"type":"hw","cmd":"spi_deinit",  "id":"spi0"}

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
        # Read scan results from mailbox
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
        # Read response from mailbox
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
        self._polarity = 0
        self._phase = 0
        self._bits = 8
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
        self._polarity = polarity
        self._phase = phase
        self._bits = bits
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
        data = list(buffer[start:end])
        _hw({"type": "hw", "cmd": "spi_write",
             "id": self._id, "data": data})

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
        out_data = list(out_buffer[out_start:out_end])
        in_len = in_end - in_start
        _hw({"type": "hw", "cmd": "spi_transfer",
             "id": self._id, "out": out_data, "in_len": in_len})
        _blinka.sync_registers()
        try:
            f = open("/spi_response", "rb")
            data = f.read()
            f.close()
            for i in range(min(in_len, len(data))):
                in_buffer[in_start + i] = data[i]
        except:
            pass
