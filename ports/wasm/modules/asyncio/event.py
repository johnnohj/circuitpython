# MicroPython asyncio module
# MIT license; Copyright (c) 2019-2020 Damien P. George
#
# Based on Adafruit CircuitPython asyncio.
# pylint: skip-file
# fmt: off

from . import core


class Event:
    """Create a new event which can be used to synchronize tasks.
    Events start in the cleared state.
    """

    def __init__(self):
        self.state = False  # False=unset; True=set
        self.waiting = core.TaskQueue()  # Queue of Tasks waiting on completion of this event

    def is_set(self):
        """Returns True if the event is set, False otherwise."""
        return self.state

    def set(self):
        """Set the event. Any tasks waiting on the event will be scheduled to run."""
        # Event becomes set, schedule any tasks waiting on it
        # Note: This must not be called from anything except the thread running
        # the asyncio loop (i.e. neither hard or soft IRQ, or a different thread).
        while self.waiting.peek():
            core._task_queue.push(self.waiting.pop())
        self.state = True

    def clear(self):
        """Clear the event."""
        self.state = False

    async def wait(self):
        """Wait for the event to be set. If the event is already set then it
        returns immediately.
        """
        if not self.state:
            # Event not set, put the calling task on the event's waiting queue
            self.waiting.push(core.cur_task)
            # Set calling task's data to the event's queue so it can be removed if needed
            core.cur_task.data = self.waiting
            # Don't reschedule - set() will reschedule us
            await core._never()
        return True


# Note: ThreadSafeFlag is not supported in WebAssembly as it requires _io_queue
# which uses select.poll() not available in browsers.