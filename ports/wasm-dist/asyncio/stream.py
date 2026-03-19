# MicroPython asyncio module - WebAssembly stream support
# MIT license; Copyright (c) 2019-2020 Damien P. George
#
# Based on Adafruit CircuitPython asyncio, adapted for WebSocket support.
# pylint: skip-file
# fmt: off

from . import core


class Stream:
    """Base class for async streams. Implements both reader and writer."""

    def __init__(self, s, e={}):
        self.s = s
        self.e = e
        self.out_buf = b""

    def get_extra_info(self, v):
        """Get extra information about the stream."""
        return self.e.get(v)

    def close(self):
        """Close the stream."""
        pass

    async def wait_closed(self):
        """Wait for the stream to close."""
        if hasattr(self.s, 'close'):
            self.s.close()

    async def read(self, n=-1):
        """Read up to n bytes and return them."""
        await core._io_queue.queue_read(self.s)
        return self.s.read(n)

    async def readinto(self, buf):
        """Read up to len(buf) bytes into buf. Returns number of bytes read."""
        await core._io_queue.queue_read(self.s)
        return self.s.readinto(buf)

    async def readexactly(self, n):
        """Read exactly n bytes and return them as a bytes object.
        Raises EOFError if the stream ends before reading n bytes.
        """
        r = b""
        while n:
            await core._io_queue.queue_read(self.s)
            r2 = self.s.read(n)
            if r2 is not None:
                if not len(r2):
                    raise EOFError
                r += r2
                n -= len(r2)
        return r

    async def readline(self):
        """Read a line and return it."""
        l = b""
        while True:
            await core._io_queue.queue_read(self.s)
            l2 = self.s.readline()
            l += l2
            if not l2 or l[-1] == 10:  # \n
                return l

    def write(self, buf):
        """Write buf to the output buffer. Call drain() to flush."""
        if not self.out_buf:
            # Try to write immediately
            ret = self.s.write(buf)
            if ret == len(buf):
                return
            if ret is not None:
                buf = buf[ret:]
        self.out_buf += buf

    async def drain(self):
        """Drain (write) all buffered output data."""
        mv = memoryview(self.out_buf)
        off = 0
        while off < len(mv):
            await core._io_queue.queue_write(self.s)
            ret = self.s.write(mv[off:])
            if ret is not None:
                off += ret
        self.out_buf = b""


# Aliases for compatibility
StreamReader = Stream
StreamWriter = Stream


class WebSocketStream:
    """Async wrapper for JavaScript WebSocket.

    Provides an asyncio-compatible interface for WebSocket connections.
    """

    # WebSocket ready states
    CONNECTING = 0
    OPEN = 1
    CLOSING = 2
    CLOSED = 3

    def __init__(self, ws):
        """Initialize with a JavaScript WebSocket object."""
        self.ws = ws
        self._read_buffer = []
        self._read_waiting = None
        self._open_waiting = None
        self._close_waiting = None
        self._error = None
        self.out_buf = b""
        self.e = {}

        # Set up JS event handlers
        ws.onmessage = self._on_message
        ws.onopen = self._on_open
        ws.onerror = self._on_error
        ws.onclose = self._on_close

    def _on_message(self, event):
        """JS callback when message received."""
        self._read_buffer.append(event.data)
        if self._read_waiting:
            core._task_queue.push(self._read_waiting)
            self._read_waiting = None

    def _on_open(self, event):
        """JS callback when connection opens."""
        if self._open_waiting:
            core._task_queue.push(self._open_waiting)
            self._open_waiting = None

    def _on_error(self, event):
        """JS callback on error."""
        self._error = event
        # Wake up any waiting tasks
        if self._read_waiting:
            core._task_queue.push(self._read_waiting)
            self._read_waiting = None
        if self._open_waiting:
            core._task_queue.push(self._open_waiting)
            self._open_waiting = None

    def _on_close(self, event):
        """JS callback when connection closes."""
        if self._close_waiting:
            core._task_queue.push(self._close_waiting)
            self._close_waiting = None
        # Wake up any tasks waiting on read
        if self._read_waiting:
            core._task_queue.push(self._read_waiting)
            self._read_waiting = None

    def get_extra_info(self, v):
        """Get extra information about the stream."""
        if v == "url":
            return self.ws.url
        if v == "readyState":
            return self.ws.readyState
        return self.e.get(v)

    async def wait_open(self):
        """Wait for the WebSocket connection to open."""
        if self.ws.readyState == self.OPEN:
            return
        if self.ws.readyState != self.CONNECTING:
            raise OSError("WebSocket not in connecting state")
        self._open_waiting = core.cur_task
        core.cur_task.data = self
        await core._never()
        if self._error:
            raise OSError("WebSocket connection failed")

    async def read(self, n=-1):
        """Read data from the WebSocket.

        Returns the next message from the receive buffer, or waits for one.
        The n parameter is ignored for WebSockets (messages are atomic).
        """
        while not self._read_buffer:
            if self.ws.readyState >= self.CLOSING:
                return b""  # Connection closed
            self._read_waiting = core.cur_task
            core.cur_task.data = self
            await core._never()
            if self._error:
                raise OSError("WebSocket error")

        data = self._read_buffer.pop(0)
        # Convert JS string to bytes if needed
        if isinstance(data, str):
            data = data.encode('utf-8')
        return data

    async def readline(self):
        """Read a line from the WebSocket.

        Note: WebSocket messages are typically not line-delimited,
        so this returns the entire next message.
        """
        return await self.read()

    def write(self, buf):
        """Write data to the WebSocket.

        For WebSockets, this sends immediately (buffering not needed).
        """
        if isinstance(buf, (bytes, bytearray, memoryview)):
            buf = bytes(buf)
        self.ws.send(buf)

    async def drain(self):
        """Drain output. WebSocket.send() is non-blocking, so this is a no-op."""
        pass

    def close(self):
        """Close the WebSocket connection."""
        if self.ws.readyState < self.CLOSING:
            self.ws.close()

    async def wait_closed(self):
        """Wait for the WebSocket to close."""
        if self.ws.readyState == self.CLOSED:
            return
        if self.ws.readyState < self.CLOSING:
            self.close()
        self._close_waiting = core.cur_task
        core.cur_task.data = self
        await core._never()


async def open_websocket(url):
    """Open a WebSocket connection to the given URL.

    Returns a tuple (reader, writer) where both are the same WebSocketStream.

    Example:
        reader, writer = await open_websocket("wss://example.com/ws")
        writer.write(b"Hello")
        response = await reader.read()
    """
    from js import WebSocket

    ws = WebSocket.new(url)
    stream = WebSocketStream(ws)

    # Wait for connection to open
    await stream.wait_open()

    return stream, stream


################################################################################
# Legacy compatibility aliases

Stream.aclose = Stream.wait_closed

async def stream_awrite(self, buf, off=0, sz=-1):
    if off != 0 or sz != -1:
        buf = memoryview(buf)
        if sz == -1:
            sz = len(buf)
        buf = buf[off : off + sz]
    self.write(buf)
    await self.drain()

Stream.awrite = stream_awrite
Stream.awritestr = stream_awrite
