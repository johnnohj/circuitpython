# MicroPython asyncio core — wasm-dist synchronous polling variant
# MIT license; Copyright (c) 2019-2024 Damien P. George
#
# Adapted from CircuitPython WASM asyncio for synchronous Worker execution.
# Key difference: no js.setTimeout — the event loop polls inline using
# time.ticks_ms(). The VM hook (mp_hal_hook) fires every 64 bytecodes
# during busy-wait, servicing bc_out drain and register sync.
#
# pylint: skip-file
# fmt: off

import sys

# Timing — use _blinka.ticks_ms() which resolves to Date.now() in WASM.
# Mask to 30 bits to match ticks_add/ticks_diff arithmetic.
try:
    from _blinka import ticks_ms as _raw_ticks
except ImportError:
    from time import ticks_ms as _raw_ticks

def ticks():
    return _raw_ticks() & 0x3FFFFFFF

def ticks_add(t, delta):
    return (t + delta) & 0x3FFFFFFF

def ticks_diff(a, b):
    diff = (a - b) & 0x3FFFFFFF
    if diff >= 0x20000000:
        diff -= 0x40000000
    return diff

# Traceback support
try:
    from traceback import print_exception
except:
    try:
        from .traceback import print_exception
    except:
        def print_exception(exc, file=sys.stderr):
            sys.print_exception(exc, file)

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

# Pause for the given number of seconds (yield to other tasks).
async def sleep_ms(t):
    return await _sleep_ms(t)

async def sleep(t):
    return await _sleep_ms(int(t * 1000))

async def _sleep_ms(ms):
    if ms > 0:
        _task_queue.push(cur_task, ticks_add(ticks(), ms))
    else:
        _task_queue.push(cur_task)
    # Yield to scheduler
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
# Don't re-schedule the object that awaits _never().
# For internal use only. Some constructs, like `await event.wait()`,
# work by NOT re-scheduling the task which calls wait(), but by
# having some other task schedule it later (e.g. Event.set(), Lock.release()).

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


# Ensure the awaitable is a task
def _promote_to_task(aw):
    return aw if isinstance(aw, Task) else create_task(aw)


################################################################################
# Async delay — yield to JS event loop with proper timing
#
# _blinka.async_sleep(ms) requests an async delay from the JS stepping driver.
# This turns busy-waits into real JS setTimeout calls, freeing the browser
# event loop during sleeps.

try:
    from _blinka import async_sleep as _async_sleep
except ImportError:
    _async_sleep = None


################################################################################
# Core event loop — cooperative polling variant

def _run_iter():
    """Run one iteration of the event loop.
    Returns the number of ms until the next task is ready, or -1 if no tasks."""
    global cur_task
    excs_all = (CancelledError, Exception)
    excs_stop = (CancelledError, StopIteration)

    t = _task_queue.peek()
    if t is None:
        cur_task = _top_level_task
        return -1  # No tasks

    dt = max(0, ticks_diff(t.ph_key, ticks()))
    if dt > 0:
        cur_task = _top_level_task
        return dt  # Next task not ready yet

    # Pop and run the next ready task
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

    return 0  # Ran a task; check again immediately


# Create and schedule a new task from a coroutine.
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
        print(context["message"], file=sys.stderr)
        print("future:", context["future"], "coro=", context["future"].coro, file=sys.stderr)
        if "exception" in context:
            print_exception(context["exception"], context["exception"], file=sys.stderr)

    def call_exception_handler(context):
        (Loop._exc_handler or Loop.default_exception_handler)(Loop, context)


_exc_context = {"message": "Task exception wasn't retrieved", "exception": None, "future": None}


def get_event_loop():
    return Loop

def new_event_loop():
    return Loop


################################################################################
# Main entry points — synchronous polling

def run_until_complete(main_coro):
    """Run a coroutine to completion.

    When running under runStepped(), sleeps yield to the JS event loop
    via _blinka.async_sleep(ms) → real setTimeout.  This replaces the
    old busy-wait approach where the VM hook was the only lifeline."""
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

    # Poll the event loop until main_task completes
    while not done:
        dt = _run_iter()
        if dt < 0:
            break  # No tasks left
        if dt > 0:
            # Request an async delay so the JS stepping driver can
            # yield to the event loop with a real setTimeout.
            # In non-stepped mode this is a no-op (no yield requested).
            if _async_sleep is not None:
                _async_sleep(dt)
            # Busy-wait until the task is ready.  In stepped mode this
            # loop exits almost immediately because the yield request
            # suspends the VM back to JS at the next branch point.
            # In non-stepped mode this is the actual delay mechanism.
            end = ticks_add(ticks(), dt)
            while ticks_diff(end, ticks()) > 0:
                pass

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
            if _async_sleep is not None:
                _async_sleep(dt)
            end = ticks_add(ticks(), dt)
            while ticks_diff(end, ticks()) > 0:
                pass


def run(coro):
    """Entry point: run a coroutine to completion."""
    return run_until_complete(coro)
