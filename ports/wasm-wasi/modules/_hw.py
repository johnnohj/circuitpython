# _hw.py — Hardware command pipe for weBlinka
#
# All hardware shims write U2IF-compatible 64-byte command packets
# to /hw/cmd. The WASI runtime intercepts these writes and routes
# them through weBlinka to the appropriate target.
#
# For read commands, the response is written to /hw/rsp by the
# WASI runtime, and we read it back.

import struct

_CMD_PATH = "/hw/cmd"
_RSP_PATH = "/hw/rsp"
REPORT_SIZE = 64

def send(opcode, *params):
    """Send a U2IF command. params are bytes to append after the opcode."""
    buf = bytearray(REPORT_SIZE)
    buf[0] = 0x00  # report ID
    buf[1] = opcode
    for i, b in enumerate(params):
        buf[2 + i] = b & 0xFF
    f = open(_CMD_PATH, "wb")
    f.write(buf)
    f.close()

def send_u16(opcode, pin, value):
    """Send a command with a pin byte and a 16-bit LE value."""
    buf = bytearray(REPORT_SIZE)
    buf[0] = 0x00
    buf[1] = opcode
    buf[2] = pin & 0xFF
    struct.pack_into("<H", buf, 3, value & 0xFFFF)
    f = open(_CMD_PATH, "wb")
    f.write(buf)
    f.close()

def send_u32(opcode, pin, value):
    """Send a command with a pin byte and a 32-bit LE value."""
    buf = bytearray(REPORT_SIZE)
    buf[0] = 0x00
    buf[1] = opcode
    buf[2] = pin & 0xFF
    struct.pack_into("<I", buf, 3, value)
    f = open(_CMD_PATH, "wb")
    f.write(buf)
    f.close()

def query(opcode, *params):
    """Send a command and return the 64-byte response."""
    send(opcode, *params)
    f = open(_RSP_PATH, "rb")
    rsp = f.read(REPORT_SIZE)
    f.close()
    return rsp

def query_u16(opcode, pin, offset=3):
    """Send a read command, return 16-bit LE value from response."""
    rsp = query(opcode, pin)
    if rsp and len(rsp) >= offset + 2:
        return struct.unpack_from("<H", rsp, offset)[0]
    return 0
