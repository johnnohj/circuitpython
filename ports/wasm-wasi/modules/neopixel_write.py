# neopixel_write.py — CircuitPython neopixel_write shim (U2IF protocol)

import struct
import _hw

_WS2812B_WRITE = 0xA2


def neopixel_write(digitalinout, buf):
    """Write NeoPixel pixel data via U2IF WS2812B_WRITE command.

    For small strips (≤58 bytes), fits in one 64-byte report.
    For larger strips, sends multiple reports (future).
    """
    pin_num = 0
    if hasattr(digitalinout, '_pin_num'):
        pin_num = digitalinout._pin_num
    elif hasattr(digitalinout, 'pin') and hasattr(digitalinout.pin, 'number'):
        pin_num = digitalinout.pin.number

    num_bytes = len(buf)
    # U2IF format: [0x00][0xA2][pin][num_bytes:u32le][pixel_data...]
    # We pack it as: report_id + opcode + pin + length + data
    packet = bytearray(_hw.REPORT_SIZE)
    packet[0] = 0x00
    packet[1] = _WS2812B_WRITE
    packet[2] = pin_num
    struct.pack_into("<I", packet, 3, num_bytes)
    # Copy as many bytes as fit in the first report
    space = _hw.REPORT_SIZE - 7
    copy_len = min(num_bytes, space)
    packet[7:7 + copy_len] = buf[:copy_len]

    f = open(_hw._CMD_PATH, "wb")
    f.write(packet)
    # If data doesn't fit in one report, write remaining as raw continuation
    if num_bytes > space:
        f.write(buf[space:])
    f.close()
