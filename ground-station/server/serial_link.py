"""serial_link.py — ESP32 USB serial receive and reconnect (S3).

Reads newline-delimited JSON from the ESP32-S3 over USB serial, keeps serial
I/O off the asyncio event loop (dedicated reader thread + bounded queue), and
reconnects with bounded backoff when the device is unplugged/reset.

Framing behavior (documented S3 contract):

- Reads are chunked (``read_chunk_size`` bytes) with a short per-read timeout
  so the reader thread notices port death and honors shutdown promptly.
- Bytes accumulate until ``\\n``; each complete line (without the newline) is
  enqueued. A partial line (no newline yet) is buffered across reads —
  partial-line recovery. If the port dies mid-line, the partial is discarded;
  state resets on the next successful open.
- A line longer than ``MAX_LINE_BYTES`` is dropped (counted + logged) up to
  the next newline; if no newline appears within the limit, the whole buffer
  is dropped.
- Complete lines cross into the event loop through a thread-safe bounded
  ``queue.Queue`` (``queue_max``). Overflow is **reported** (counted +
  logged), never silently dropped. :meth:`queue.Queue` is used instead of
  :class:`asyncio.Queue` because the producer is the serial reader thread,
  not the asyncio event loop; the consumer side bridges via
  :func:`asyncio.to_thread`.

Link states (exposed via :attr:`state`; mapped to the dashboard ``link``
contract by the watchdog in ``main.py``):

- ``"disconnected"``  — port not open (startup, after :meth:`stop`)
- ``"reconnecting"``  — open failed; bounded backoff retry in progress
- ``"connected"``     — port open, reading
"""

from __future__ import annotations

import asyncio
import logging
import queue
import threading
import time
from typing import AsyncIterator, Optional

import serial

from protocol import MAX_LINE_BYTES, QUEUE_MAX, RECONNECT_MAX_S, RECONNECT_MIN_S

log = logging.getLogger("serial_link")

_SENTINEL = object()

# Per-read chunking (not the S1 READ_TIMEOUT_S watchdog — that one drives the
# dashboard link state and lives in main.py).
READ_CHUNK_SIZE = 512
READ_CHUNK_TIMEOUT_S = 0.1


class SerialLink:
    """Receives newline-delimited JSON lines from the ESP32 over USB serial.

    Serial I/O runs in a dedicated reader thread (never on the asyncio event
    loop). Reconnection uses bounded exponential backoff
    (``reconnect_min_s`` doubling up to ``reconnect_max_s``) and cannot block
    the WebSocket server.

    ``serial_class`` defaults to :class:`serial.Serial`; tests inject a
    scripted fake so no hardware is required (S3 pass criterion: "test with
    a scripted serial producer or virtual loopback").
    """

    def __init__(self, port: str, baud: int = 115200,
                 serial_class=None,
                 queue_max: int = QUEUE_MAX,
                 reconnect_min_s: float = RECONNECT_MIN_S,
                 reconnect_max_s: float = RECONNECT_MAX_S,
                 read_chunk_size: int = READ_CHUNK_SIZE,
                 read_chunk_timeout_s: float = READ_CHUNK_TIMEOUT_S):
        self.port = port
        self.baud = baud
        self._serial_class = serial_class if serial_class is not None else serial.Serial
        self.queue_max = queue_max
        self.reconnect_min_s = reconnect_min_s
        self.reconnect_max_s = reconnect_max_s
        self.read_chunk_size = read_chunk_size
        self.read_chunk_timeout_s = read_chunk_timeout_s

        self._queue: "queue.Queue" = queue.Queue(maxsize=queue_max)
        self._stop_event = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._serial = None
        self._state = "disconnected"
        self._last_line_time: Optional[float] = None
        self.stats = {
            "lines": 0,             # complete lines enqueued
            "oversize_dropped": 0,   # lines dropped for exceeding MAX_LINE_BYTES
            "queue_dropped": 0,      # lines dropped on queue overflow (reported)
            "opens": 0,             # successful port opens (incl. first)
            "open_failures": 0,      # failed port opens
        }

    @property
    def state(self) -> str:
        """One of ``disconnected`` | ``reconnecting`` | ``connected``."""
        return self._state

    @property
    def last_line_time(self) -> Optional[float]:
        """Monotonic time of the last complete line enqueued, or ``None``.

        Initialized to the open time on each fresh open, so after a reconnect
        the link reads as idle (watchdog forces ``lost`` once the S1 read
        timeout elapses with no data) until real data arrives.
        """
        return self._last_line_time

    def _set_state(self, state: str) -> None:
        if state != self._state:
            log.info("serial link %s -> %s (%s)", self._state, state, self.port)
            self._state = state

    async def start(self) -> None:
        """Open the serial port (in the reader thread) and begin reading.

        If the device is missing at startup the link goes into
        ``"reconnecting"`` and retries with bounded backoff; the rest of the
        server keeps running (S3 pass criterion: serial missing at startup).
        """
        if self._thread is not None:
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._reader, name="serial-reader", daemon=True)
        self._thread.start()

    def _reader(self) -> None:
        """Reader thread: open the port, frame lines, reconnect with backoff."""
        backoff = self.reconnect_min_s
        while not self._stop_event.is_set():
            ser = None
            try:
                ser = self._serial_class(self.port, self.baud,
                                         timeout=self.read_chunk_timeout_s)
            except Exception as e:
                self.stats["open_failures"] += 1
                self._set_state("reconnecting")
                log.warning("serial open failed (%s): %s; retrying in %.1f s",
                            self.port, e, backoff)
                self._sleep(backoff)
                backoff = min(self.reconnect_max_s, backoff * 2)
                continue
            # Opened successfully.
            self._serial = ser
            # Idle is measured from the open (S1 contract: "no data for
            # READ_TIMEOUT_S => idle/lost"), not from "no data ever".
            self._last_line_time = time.monotonic()
            self.stats["opens"] += 1
            self._set_state("connected")
            log.info("serial connected: %s @ %d baud", self.port, self.baud)
            backoff = self.reconnect_min_s  # reset backoff on success
            self._read_loop(ser)
            # _read_loop returns when the port died (OSError/EOF) or on stop.
            self._serial = None
            try:
                ser.close()
            except Exception:
                pass
            if self._stop_event.is_set():
                break
            # Port died mid-read (unplug/reset). Reconnect.
            self._set_state("reconnecting")
            log.warning("serial lost (%s); reconnecting in %.1f s", self.port, backoff)
            self._sleep(backoff)
            backoff = min(self.reconnect_max_s, backoff * 2)
        self._set_state("disconnected")

    def _sleep(self, seconds: float) -> None:
        """Interruptible sleep: checks the stop event in small increments so
        :meth:`stop` is honored promptly during backoff."""
        deadline = time.monotonic() + seconds
        while not self._stop_event.is_set():
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            self._stop_event.wait(min(0.05, remaining))

    def _read_loop(self, ser) -> None:
        """Frame newline-terminated lines from the open port into the queue.

        ``buf`` accumulates bytes until ``\\n``; each complete line (without
        the newline) is length-checked before enqueueing. A line longer than
        ``MAX_LINE_BYTES`` is dropped (counted + logged).

        If a partial line exceeds ``MAX_LINE_BYTES`` without a newline, the
        reader enters a ``discarding`` state that persists across reads until
        the next newline: all bytes before that delimiter belong to the same
        oversized line and must not be interpreted as a new message. The
        oversized line is counted exactly once. If a valid newline-terminated
        message follows the delimiter in the same chunk, it is processed
        normally.

        While discarding, bytes are never retained — each chunk is scanned
        for its first newline; the prefix is dropped, and only the suffix
        after the delimiter is kept for normal framing.
        """
        buf = b""
        discarding = False  # True while dropping an oversized line awaiting its newline
        while not self._stop_event.is_set():
            try:
                chunk = ser.read(self.read_chunk_size)
            except (OSError, serial.SerialException) as e:
                log.warning("serial read failed (%s): %s", self.port, e)
                return  # port died; the caller handles reconnect
            if not chunk:
                continue  # read timeout, no data — port is fine, just idle

            if discarding:
                # We are mid-oversize-line: hunt the newline in this chunk,
                # dropping everything before it (do not retain discarded bytes —
                # memory stays bounded even for an endless no-newline stream).
                nl = chunk.find(b"\n")
                if nl == -1:
                    continue  # whole chunk belongs to the oversized line; drop it
                # Resume framing at the byte after the delimiter.
                buf = chunk[nl + 1:]
                discarding = False
            else:
                buf += chunk

            # Frame every complete line in buf.
            while b"\n" in buf:
                i = buf.index(b"\n")
                line = buf[:i]
                buf = buf[i + 1:]
                if len(line) > MAX_LINE_BYTES:
                    self.stats["oversize_dropped"] += 1
                    log.warning("oversize line (> %d bytes) dropped", MAX_LINE_BYTES)
                else:
                    self._enqueue(line)

            # A partial line with no newline that exceeds the limit: drop it once,
            # count once, and stay discarding until the next newline.
            if not discarding and len(buf) > MAX_LINE_BYTES:
                self.stats["oversize_dropped"] += 1
                log.warning("oversize line (> %d bytes) dropped", MAX_LINE_BYTES)
                discarding = True
                buf = b""
        # Stop: discard any partial line (partial-line recovery on shutdown).

    def _enqueue(self, line: bytes) -> None:
        """Put one complete line on the bounded queue (overflow reported).

        Uses :meth:`queue.Queue.put_nowait` (thread-safe) because this is called
        from the reader thread, not the asyncio event loop.
        """
        try:
            self._queue.put_nowait(line)
            self.stats["lines"] += 1
            self._last_line_time = time.monotonic()
        except queue.Full:
            self.stats["queue_dropped"] += 1
            log.warning("serial queue full (%d lines); dropping line "
                        "(overflow reported, not silent)", self.queue_max)

    async def lines(self) -> AsyncIterator[bytes]:
        """Yield complete newline-terminated lines (without the ``\\n``).

        Terminates when :meth:`stop` is called (a sentinel is enqueued).

        Consumes from a thread-safe ``queue.Queue`` via ``asyncio.to_thread``,
        since :meth:`queue.Queue.get`` blocks and cannot run on the event loop
        thread directly.
        """
        while True:
            line = await asyncio.to_thread(self._queue.get)
            if line is _SENTINEL:
                return
            yield line

    async def stop(self) -> None:
        """Stop the reader thread and close the serial port cleanly.

        Idempotent; safe to call before :meth:`start` or more than once.
        """
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            if self._thread.is_alive():
                log.warning("serial reader thread did not exit within 2 s")
            self._thread = None
        # Wake the lines() iterator with a sentinel, making room if needed.
        while True:
            try:
                self._queue.put_nowait(_SENTINEL)
                break
            except queue.Full:
                try:
                    self._queue.get_nowait()  # make room; counted as dropped
                    self.stats["queue_dropped"] += 1
                except queue.Empty:
                    self._queue.put_nowait(_SENTINEL)  # must fit now
                    break
