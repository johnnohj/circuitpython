# _ws_bus.py — Protocol bus interface for WS protocol wrappers.
#
# This module provides the bridge between Python-level hardware
# operations and the sync bus.  Each wrapper module calls emit()
# to broadcast state changes as WS-aligned protocol messages.
#
# In the simulated board, emit() writes to a ringbuffer that JS
# reads each frame.  For weBlinka (external boards), the same
# messages are serialized over WebSerial/USB/BLE.

import sys

# Protocol message types (match ws-protocol.mjs)
PIN_CONFIG = 'pin_config'
PIN_EVENT = 'pin_event'
PIXELS_CREATE = 'pixels_create'
PIXELS_WRITE = 'pixels_write'
PIXELS_DELETE = 'pixels_delete'
I2C_INIT = 'i2c_init'
I2C_EVENT = 'i2c_event'

_listeners = []

def on_message(callback):
    """Register a listener for outbound protocol messages."""
    _listeners.append(callback)

def emit(msg_type, **fields):
    """Broadcast a protocol message to all listeners."""
    msg = {'type': msg_type}
    msg.update(fields)
    for cb in _listeners:
        try:
            cb(msg)
        except Exception:
            pass
