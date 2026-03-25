# busio.py — CircuitPython busio shim (U2IF protocol)

import struct
import _hw

# I2C opcodes
_I2C_INIT   = 0x80
_I2C_DEINIT = 0x81
_I2C_WRITE  = 0x82
_I2C_READ   = 0x83

# SPI opcodes
_SPI_INIT   = 0x60
_SPI_DEINIT = 0x61
_SPI_WRITE  = 0x62
_SPI_READ   = 0x63

# UART opcodes
_UART_INIT   = 0xB0
_UART_DEINIT = 0xB1
_UART_WRITE  = 0xB2
_UART_READ   = 0xB3
_UART_ANY    = 0xB4


# ── I2C ──────────────────────────────────────────────────────────────

class I2C:
    def __init__(self, scl, sda, frequency=100000):
        self._scl = scl
        self._sda = sda
        self._frequency = frequency
        self._locked = False
        # Init: [pullup=0][baudrate:u32le]
        buf = bytearray(_hw.REPORT_SIZE)
        buf[0] = 0x00
        buf[1] = _I2C_INIT
        buf[2] = 0x00  # no pullup
        struct.pack_into("<I", buf, 3, frequency)
        f = open(_hw._CMD_PATH, "wb")
        f.write(buf)
        f.close()

    def deinit(self):
        _hw.send(_I2C_DEINIT)
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
        # No direct U2IF scan command — would need to probe each address
        # For simulation, return empty list
        return []

    def writeto(self, address, buffer, *, start=0, end=None):
        if end is None:
            end = len(buffer)
        data = buffer[start:end]
        if len(data) == 0:
            return
        # [0x00][I2C_WRITE][addr][stop=1][size:u32le][data...]
        buf = bytearray(_hw.REPORT_SIZE)
        buf[0] = 0x00
        buf[1] = _I2C_WRITE
        buf[2] = address & 0x7F
        buf[3] = 0x01  # stop
        struct.pack_into("<I", buf, 4, len(data))
        space = _hw.REPORT_SIZE - 8
        copy_len = min(len(data), space)
        buf[8:8 + copy_len] = data[:copy_len]
        f = open(_hw._CMD_PATH, "wb")
        f.write(buf)
        f.close()

    def readfrom_into(self, address, buffer, *, start=0, end=None):
        if end is None:
            end = len(buffer)
        length = end - start
        # [0x00][I2C_READ][addr][stop=1][size:u32le]
        rsp = _hw.query(_I2C_READ, address & 0x7F, 0x01,
                        length & 0xFF, (length >> 8) & 0xFF,
                        (length >> 16) & 0xFF, (length >> 24) & 0xFF)
        if rsp:
            for i in range(length):
                if 2 + i < len(rsp):
                    buffer[start + i] = rsp[2 + i]
                else:
                    buffer[start + i] = 0xFF

    def writeto_then_readfrom(self, address, out_buffer, in_buffer,
                               *, out_start=0, out_end=None,
                               in_start=0, in_end=None):
        self.writeto(address, out_buffer, start=out_start, end=out_end)
        self.readfrom_into(address, in_buffer, start=in_start, end=in_end)


# ── SPI ──────────────────────────────────────────────────────────────

class SPI:
    def __init__(self, clock, MOSI=None, MISO=None, half_duplex=False):
        self._clock = clock
        self._mosi = MOSI
        self._miso = MISO
        self._locked = False
        self._baudrate = 1000000
        self._polarity = 0
        self._phase = 0
        self._bits = 8
        # Init: [baudrate:u32le]
        buf = bytearray(_hw.REPORT_SIZE)
        buf[0] = 0x00
        buf[1] = _SPI_INIT
        struct.pack_into("<I", buf, 2, self._baudrate)
        f = open(_hw._CMD_PATH, "wb")
        f.write(buf)
        f.close()

    def deinit(self):
        _hw.send(_SPI_DEINIT)
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
        data = buf[start:end]
        # [0x00][SPI_WRITE][chunk_len][data...]
        packet = bytearray(_hw.REPORT_SIZE)
        packet[0] = 0x00
        packet[1] = _SPI_WRITE
        packet[2] = min(len(data), 61)  # max 61 bytes per report
        copy_len = min(len(data), 61)
        packet[3:3 + copy_len] = data[:copy_len]
        f = open(_hw._CMD_PATH, "wb")
        f.write(packet)
        f.close()

    def readinto(self, buf, *, start=0, end=None, write_value=0):
        if end is None:
            end = len(buf)
        length = end - start
        # [0x00][SPI_READ][write_byte][num_bytes]
        rsp = _hw.query(_SPI_READ, write_value, length)
        if rsp:
            for i in range(length):
                if 2 + i < len(rsp):
                    buf[start + i] = rsp[2 + i]
                else:
                    buf[start + i] = write_value

    def write_readinto(self, out_buf, in_buf, *,
                        out_start=0, out_end=None,
                        in_start=0, in_end=None):
        self.write(out_buf, start=out_start, end=out_end)
        self.readinto(in_buf, start=in_start, end=in_end)


# ── UART ─────────────────────────────────────────────────────────────

class UART:
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
        # Use port-specific opcode offset (port 0 = 0xB0, port 1 = 0xBA)
        self._base = _UART_INIT + (self._port * 0x0A)
        buf = bytearray(_hw.REPORT_SIZE)
        buf[0] = 0x00
        buf[1] = self._base  # UART_INIT
        struct.pack_into("<I", buf, 2, baudrate)
        f = open(_hw._CMD_PATH, "wb")
        f.write(buf)
        f.close()

    def deinit(self):
        _hw.send(self._base + 1)  # UART_DEINIT

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.deinit()

    def read(self, nbytes=None):
        if nbytes is None:
            nbytes = 64
        rsp = _hw.query(self._base + 3, nbytes)  # UART_READ
        if rsp and rsp[1] == 0x01:
            data = bytes(rsp[2:2 + nbytes])
            return data if any(b != 0 for b in data) else None
        return None

    def readline(self):
        # Read up to 64 bytes and look for newline
        data = self.read(64)
        if data:
            nl = data.find(b'\n')
            if nl >= 0:
                return data[:nl + 1]
            return data
        return None

    def write(self, buf):
        # [0x00][UART_WRITE][chunk_len][data...]
        packet = bytearray(_hw.REPORT_SIZE)
        packet[0] = 0x00
        packet[1] = self._base + 2  # UART_WRITE
        copy_len = min(len(buf), 61)
        packet[2] = copy_len
        packet[3:3 + copy_len] = buf[:copy_len]
        f = open(_hw._CMD_PATH, "wb")
        f.write(packet)
        f.close()
        return len(buf)

    @property
    def in_waiting(self):
        rsp = _hw.query(self._base + 4)  # UART_ANY
        if rsp and rsp[1] == 0x01:
            return rsp[2]
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
        pass
