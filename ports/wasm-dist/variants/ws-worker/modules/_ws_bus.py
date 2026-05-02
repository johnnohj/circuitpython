# _ws_bus.py — Protocol bus interface for WS protocol wrappers.
#
# Protocol messages are written to fd 4 (the protocol channel),
# bypassing the serial path entirely.  The WASI shim routes fd 4
# to the onProtocol callback.  User-visible serial output (print,
# REPL) goes through fd 1 (stdout) → serial_tx ring → displayio
# terminal as normal.
#
# fd 0 = stdin, fd 1 = stdout (serial), fd 2 = stderr, fd 3 = root
# preopen, fd 4 = protocol channel.

import json
import os

# Protocol message types (match ws-protocol.mjs)
PIN_CONFIG = 'pin_config'
PIN_EVENT = 'pin_event'
PIXELS_CREATE = 'pixels_create'
PIXELS_WRITE = 'pixels_write'
PIXELS_DELETE = 'pixels_delete'
I2C_INIT = 'i2c_init'
I2C_EVENT = 'i2c_event'

_PROTO_FD = 4
_listeners = []

def on_message(callback):
    """Register a listener for outbound protocol messages."""
    _listeners.append(callback)

def emit(msg_type, **fields):
    """Broadcast a protocol message on the protocol channel (fd 4)."""
    msg = {'type': msg_type}
    msg.update(fields)

    # Write to fd 4 — bypasses serial/displayio entirely
    try:
        line = json.dumps(msg) + '\n'
        os.write(_PROTO_FD, line)
    except Exception:
        pass

    for cb in _listeners:
        try:
            cb(msg)
        except Exception:
            pass
