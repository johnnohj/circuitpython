# MicroPython asyncio core — wasm-dist cooperative yield variant
# MIT license; Copyright (c) 2019-2024 Damien P. George
#
# Adapted for the WASM-dist port's cooperative yield architecture.
# Key difference from upstream: when all asyncio tasks are sleeping,
# the event loop calls time.sleep() which triggers YIELD_SLEEP in
# the supervisor.  This lets the scheduler run other contexts instead
# of busy-waiting inside the event loop.
#
# pylint: skip-file
# fmt: off

import sys
import time

# Timing — must match the C-level ticks() in modasyncio.c exactly:
#   _TICKS_PERIOD = 1 << 29  (29-bit wrapping)
#   ticks() = (supervisor_ticks_ms64() + 0x1fff0000) % (1 << 29)
# CircuitPython doesn't have time.ticks_ms (that's MicroPython),
# so we derive from time.monotonic_ns() for precision.
_TICKS_PERIOD = 1 << 29
_TICKS_MAX = _TICKS_PERIOD - 1
_TICKS_HALF = _TICKS_PERIOD >> 1
_TICKS_OFFSET = 0x1fff0000

def ticks():
    ms = time.monotonic_ns() // 1_000_000
    return (ms + _TICKS_OFFSET) % _TICKS_PERIOD

def ticks_add(t, delta):
    return (t + delta) % _TICKS_PERIOD

def ticks_diff(a, b):
    diff = (a - b) % _TICKS_PERIOD
    if diff >= _TICKS_HALF:
        diff -= _TICKS_PERIOD
    return diff

# Traceback support
# sys.stderr may not exist (MICROPY_PY_SYS_STDFILES=0 in WASM port),
# so fall back to printing to stdout via mp_hal.
_stderr = getattr(sys, 'stderr', None)

def print_exception(exc, file=None):
    if file is None:
        file = _stderr
    if file is not None:
        sys.print_exception(exc, file)
    else:
        sys.print_exception(exc)

# Import TaskQueue and Task from built-in C code.
from _asyncio import TaskQueue, Task


################################################################################
# Exceptions

try:
    from _asyncio import CancelledError, InvalidStateError
except (ImportError, AttributeError):
    class CancelledError(BaseException):
        pass

    class InvalidStateError(Exception):
        pass

class TimeoutError(Exception):
    pass


################################################################################
# Sleep

async def sleep_ms(t):
    return await _sleep_ms(t)

async def sleep(t):
    return await _sleep_ms(int(t * 1000))

async def _sleep_ms(ms):
    if ms > 0:
        _task_queue.push(cur_task, ticks_add(ticks(), ms))
    else:
        _task_queue.push(cur_task)
    return (yield)


################################################################################
# Queue, scheduling

_task_queue = TaskQueue()

class _TopLevelCoro:
    def send(self, v):
        return v
    def throw(self, *args):
        return args[0]
TopLevelCoro = _TopLevelCoro()


################################################################################
# "Never schedule" object

class _NeverSingletonGenerator:
    def __init__(self):
        self.state = None
        self.exc = StopIteration()

    def __iter__(self):
        return self

    def __await__(self):
        return self

    def __next__(self):
        if self.state is not None:
            self.state = None
            return None
        else:
            self.exc.__traceback__ = None
            raise self.exc


def _never(sgen=_NeverSingletonGenerator()):
    sgen.state = False
    return sgen


def _promote_to_task(aw):
    return aw if isinstance(aw, Task) else create_task(aw)


################################################################################
# Core event loop — cooperative yield variant
#
# When all tasks are sleeping for dt ms, the event loop calls
# time.sleep(dt / 1000) instead of busy-waiting.  In the WASM port,
# time.sleep() triggers mp_hal_delay_ms() which sets the context to
# CTX_SLEEPING and yields to the supervisor.  The scheduler then runs
# other contexts until the deadline passes.

def _run_iter():
    """Run one iteration of the event loop.
    Returns the number of ms until the next task is ready, or -1 if no tasks."""
    global cur_task
    excs_all = (CancelledError, Exception)
    excs_stop = (CancelledError, StopIteration)

    t = _task_queue.peek()
    if t is None:
        cur_task = _top_level_task
        return -1

    dt = max(0, ticks_diff(t.ph_key, ticks()))
    if dt > 0:
        cur_task = _top_level_task
        return dt

    t = _task_queue.pop()
    cur_task = t
    try:
        exc = t.data
        if not exc:
            t.coro.send(None)
        else:
            t.data = None
            t.coro.throw(exc)
    except excs_all as er:
        assert t.data is None
        if t.state:
            waiting = False
            if t.state is True:
                t.state = None
            elif callable(t.state):
                t.state(t, er)
                t.state = False
                waiting = True
            else:
                while t.state.peek():
                    _task_queue.push(t.state.pop())
                    waiting = True
                t.state = False
            if not waiting and not isinstance(er, excs_stop):
                _task_queue.push(t)
            t.data = er
        elif t.state is None:
            t.data = exc
            _exc_context["exception"] = exc
            _exc_context["future"] = t
            Loop.call_exception_handler(_exc_context)

    return 0


def create_task(coro):
    if not hasattr(coro, "send"):
        raise TypeError("coroutine expected")
    t = Task(coro, globals())
    _task_queue.push(t)
    return t


_top_level_task = Task(TopLevelCoro, globals())

################################################################################
# Event loop wrapper

cur_task = _top_level_task

class Loop:
    _exc_handler = None

    def create_task(coro):
        return create_task(coro)

    def run_until_complete(coro):
        return run_until_complete(coro)

    def run_forever():
        run_forever()

    def stop():
        pass

    def close():
        pass

    def set_exception_handler(handler):
        Loop._exc_handler = handler

    def get_exception_handler():
        return Loop._exc_handler

    def default_exception_handler(loop, context):
        if _stderr is not None:
            print(context["message"], file=_stderr)
            print("future:", context["future"], "coro=", context["future"].coro, file=_stderr)
        else:
            print(context["message"])
            print("future:", context["future"], "coro=", context["future"].coro)
        if "exception" in context:
            print_exception(context["exception"])

    def call_exception_handler(context):
        (Loop._exc_handler or Loop.default_exception_handler)(Loop, context)


_exc_context = {"message": "Task exception wasn't retrieved", "exception": None, "future": None}


def get_event_loop():
    return Loop

def new_event_loop():
    return Loop


################################################################################
# Main entry points — cooperative yield

def run_until_complete(main_coro):
    """Run a coroutine to completion.

    When all tasks are sleeping, time.sleep() yields cooperatively to
    the supervisor scheduler, allowing other contexts to run."""
    global cur_task
    main_task = create_task(main_coro)
    done = False
    result = None
    exc = None

    def _on_done(t, er):
        nonlocal done, result, exc
        done = True
        if isinstance(er, StopIteration):
            result = er.value
        elif not isinstance(er, CancelledError):
            exc = er

    main_task.state = _on_done

    while not done:
        dt = _run_iter()
        if dt < 0:
            break
        if dt > 0:
            # Cooperatively yield to the supervisor scheduler.
            # time.sleep() calls mp_hal_delay_ms() which sets
            # CTX_SLEEPING and yields — no busy-wait.
            time.sleep(dt / 1000)

    cur_task = _top_level_task

    if exc is not None:
        raise exc
    return result


def run_forever():
    """Run the event loop forever (until no tasks remain or interrupted)."""
    while True:
        dt = _run_iter()
        if dt < 0:
            break
        if dt > 0:
            time.sleep(dt / 1000)


def run(coro):
    """Entry point: run a coroutine to completion."""
    return run_until_complete(coro)
