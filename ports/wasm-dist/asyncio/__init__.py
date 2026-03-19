# MicroPython asyncio module — wasm-dist synchronous variant
# MIT license; Copyright (c) 2024 Damien P. George
# Adapted for synchronous Worker execution (no js.setTimeout)

from .core import *
from .funcs import wait_for, wait_for_ms, gather
from .event import Event
from .lock import Lock

__version__ = (3, 0, 0)
