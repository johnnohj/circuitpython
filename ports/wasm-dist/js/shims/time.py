# time.py — CircuitPython time shim (wasm-dist simulator)
#
# Replaces the builtin time module with simulator-aware versions.
# sleep() is the cooperative yield point for the supervisor:
#   1. Emits time_sleep bc_out event (tells JS to pace execution)
#   2. Calls sync_registers() to pull latest hardware state from bc_in
#   3. Returns immediately (does NOT actually block WASM)

import _blinka
import json as _json

def _hw(msg):
    _blinka.send(_json.dumps(msg))


def sleep(seconds):
    """Request a real async delay and sync hardware registers.

    In stepped mode (runStepped), this yields to the JS event loop
    for the specified duration via a real setTimeout.  Also emits a
    time_sleep bc_out event so the JS host can observe sleep calls."""
    ms = int(seconds * 1000)
    if ms < 0:
        ms = 0
    _hw({"type": "hw", "cmd": "time_sleep", "ms": ms})
    _blinka.async_sleep(ms)
    _blinka.sync_registers()


_t0 = _blinka.ticks_ms()

def monotonic():
    """Return elapsed time in seconds (float) since module load."""
    return (_blinka.ticks_ms() - _t0) / 1000.0


def monotonic_ns():
    """Return elapsed time in nanoseconds since module load."""
    return (_blinka.ticks_ms() - _t0) * 1_000_000


def struct_time(tm):
    """Minimal struct_time stub."""
    return tm


def localtime(secs=None):
    """Stub: returns a dummy localtime tuple."""
    return (2026, 1, 1, 0, 0, 0, 0, 1)
