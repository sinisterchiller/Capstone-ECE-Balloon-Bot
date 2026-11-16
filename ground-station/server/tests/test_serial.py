"""Tests for serial_link.py + main.py ingestion/watchdog (S3).

Uses a scripted serial producer (FakeSerial) so no hardware is required —
this is the S3 pass criterion "test with a scripted serial producer or
virtual loopback before real ESP32 hardware". The producer emulates the
pyserial ``Serial`` interface: chunked ``read()`` with timeout, ``close()``,
and USB removal (reads raise OSError after ``close()``).

Run:
    python3 -m unittest tests.test_serial -v
"""

import asyncio
import json
import threading
import time
import unittest

import serial

from main import ingest_loop, link_watchdog
from protocol import MAX_LINE_BYTES
from serial_link import SerialLink
from tests.test_websocket import TELEMETRY, free_port, line, wait_for
from websocket_server import MissionState, WebSocketServer

from websockets.asyncio.client import connect


async def recv_frame(client, timeout: float = 5.0) -> str:
    """Recv one WebSocket text frame, failing fast instead of hanging forever.

    A bare ``client.recv()`` has no default timeout, so if the expected frame
    never arrives (because an earlier assertion failed or a regression dropped
    it) the coroutine would block indefinitely and deadlock the test. Wrapping
    it with ``asyncio.wait_for`` turns a missing frame into an immediate
    ``AssertionError``.
    """
    try:
        return await asyncio.wait_for(client.recv(), timeout=timeout)
    except asyncio.TimeoutError:
        raise AssertionError(f"timed out waiting for WS frame after {timeout}s")


class FakeSerial:
    """Scripted serial producer emulating pyserial's ``Serial`` interface.

    The test drives it via ``write(data)`` (device writes) and ``close()``
    (USB removal: subsequent reads raise OSError, like a real unplug).
    ``read(n)`` honors the per-read timeout and returns ``b""`` when idle.
    """

    def __init__(self, port, baud=115200, timeout=0.1):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.is_open = True
        self._buf = bytearray()
        self._cv = threading.Condition()

    def read(self, n=1):
        with self._cv:
            deadline = time.monotonic() + (self.timeout or 0.1)
            while True:
                if not self.is_open:
                    raise OSError(6, "No such device or address")
                if len(self._buf) > 0:
                    data = bytes(self._buf[:n])
                    del self._buf[:n]
                    return data
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    return b""
                self._cv.wait(timeout=min(0.05, remaining))

    def write(self, data):
        with self._cv:
            self._buf.extend(data)
            self._cv.notify()

    def close(self):
        with self._cv:
            self.is_open = False
            self._cv.notify_all()


class SerialFactory:
    """Stands in for ``serial.Serial``; emulates plug/unplug of the device.

    Records open timestamps (for backoff checks). While ``plugged`` is
    False, opens raise (device missing); when True, the same FakeSerial
    instance is returned and re-opened (USB reattachment at the same path).
    """

    def __init__(self, plugged: bool = True):
        self.plugged = plugged
        self.instance: FakeSerial | None = None
        self.open_times: list[float] = []

    def __call__(self, port, baud, timeout=None):
        self.open_times.append(time.monotonic())
        if not self.plugged:
            raise serial.SerialException(f"device {port} not found")
        if self.instance is None:
            self.instance = FakeSerial(port, baud, timeout)
        else:
            self.instance.is_open = True  # replug: reads work again
        return self.instance


class TestSerialLink(unittest.TestCase):
    """End-to-end: scripted serial producer -> SerialLink -> WS server -> client."""

    def test_valid_line_reaches_ws_client(self):
        """Well-formed serial messages reach the dashboard unchanged apart
        from documented normalization."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port, MissionState(mission="Test"))
            await server.start()
            factory = SerialFactory()
            link = SerialLink("fake-port", 115200, serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            task = asyncio.create_task(ingest_loop(server, link))
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            hello = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(hello["type"], "hello")
            factory.instance.write(line(TELEMETRY))
            frame_raw = await recv_frame(client, timeout=5.0)
            self.assertIsInstance(frame_raw, str, "must be a text frame")
            frame = json.loads(frame_raw)
            self.assertEqual(frame["type"], "telemetry")
            self.assertEqual(frame["seq"], TELEMETRY["seq"])
            self.assertEqual(frame["ts"], TELEMETRY["ts"])
            self.assertEqual(frame["alt"], TELEMETRY["alt"])
            self.assertEqual(frame["mode"], "NORMAL")  # documented default
            self.assertTrue(server.stats["received"] == 1)
            task.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_malformed_lines_no_crash_next_valid_ok(self):
        """Malformed JSON, truncated JSON, and invalid UTF-8 are rejected
        without crashing; the next valid line is processed normally."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            task = asyncio.create_task(ingest_loop(server, link))
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await recv_frame(client, timeout=5.0)  # hello

            # 1. Not JSON at all.
            factory.instance.write(b"not json\n")
            # 2. Truncated JSON (complete line, malformed object).
            factory.instance.write(b'{"kind": "telemetry", "seq": \n')
            # 3. Invalid UTF-8.
            factory.instance.write(b'{"kind": "telemetry", "seq": 1, "ts": "\xff\xfe"}\n')
            # 4. Valid line — must be accepted.
            factory.instance.write(line(TELEMETRY))

            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["type"], "telemetry")
            self.assertEqual(frame["seq"], TELEMETRY["seq"])
            self.assertEqual(server.stats["rejected"], 3)
            self.assertEqual(server.stats["received"], 1)
            task.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_partial_line_recovery(self):
        """A line split across many reads (no newline until the end) is
        reassembled into one complete line (partial-line recovery)."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              read_chunk_size=8,  # force many small reads
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            task = asyncio.create_task(ingest_loop(server, link))
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await recv_frame(client, timeout=5.0)  # hello

            full = line(TELEMETRY)
            for i in range(0, len(full), 7):  # drip-feed in 7-byte chunks
                factory.instance.write(full[i:i + 7])
                await asyncio.sleep(0.02)
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["type"], "telemetry")
            self.assertEqual(frame["seq"], TELEMETRY["seq"])
            self.assertEqual(frame["alt"], TELEMETRY["alt"])
            task.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_oversize_line_dropped_next_line_ok(self):
        """An oversized line (> MAX_LINE_BYTES) is dropped and counted exactly
        once; the reader never retains the discarded bytes (bounded buffer) and
        resumes framing at the next newline.

        Covers (per the S3 contract):
        * Normal mode handles multiple newline-delimited lines in one chunk,
          including an oversized complete line followed by valid lines.
        * A JSON-looking suffix that is still part of the oversized line (i.e.
          before the delimiting newline) must NOT be forwarded.
        * A valid line arriving after that delimiter in the same chunk MUST be
          forwarded.
        * A long no-newline stream split across many reads is counted once and
          the reader stays live until a terminating newline is read.
        """
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            task = asyncio.create_task(ingest_loop(server, link))
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await recv_frame(client, timeout=5.0)  # hello

            # --- Case 1: multiple lines in one chunk — an oversized complete
            # line immediately followed by two valid lines. Both valids are
            # forwarded and the oversize is counted once. ---
            big = b"a" * (MAX_LINE_BYTES + 100) + b"\n"
            factory.instance.write(big + line({**TELEMETRY, "seq": 1})
                                    + line({**TELEMETRY, "seq": 2}))
            await wait_for(lambda: link.stats["oversize_dropped"] == 1, timeout=2.0)
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["type"], "telemetry")
            self.assertEqual(frame["seq"], 1)
            self.assertEqual(link.stats["oversize_dropped"], 1)
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["seq"], 2)
            self.assertEqual(link.stats["oversize_dropped"], 1)
            self.assertEqual(server.stats["received"], 2)

            # --- Case 2: an oversized line without a newline first (crosses
            # reads), then a chunk holding a JSON-looking suffix + the
            # delimiting newline + a genuine valid line. The suffix belongs
            # to the oversized line -> NOT forwarded; only the valid line
            # after the newline is. Counted exactly once. ---
            body = b"y" * (MAX_LINE_BYTES + 64)            # no newline, crosses reads
            json_suffix = b'{"kind": "telemetry", "seq": 99, "ts": "bogus"'  # part of body
            factory.instance.write(body + json_suffix + b"\n" + line({**TELEMETRY, "seq": 3}))
            await wait_for(lambda: link.stats["oversize_dropped"] == 2, timeout=2.0)
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["type"], "telemetry")
            self.assertEqual(frame["seq"], 3)
            self.assertEqual(server.stats["rejected"], 0)     # JSON suffix not forwarded
            self.assertEqual(link.stats["oversize_dropped"], 2)
            self.assertEqual(server.stats["received"], 3)

            # --- Case 3: a long no-newline stream (many oversize chunks) —
            # the oversized line is counted once, discarded bytes are not
            # retained, the reader stays live, and a valid line after the
            # terminating newline is forwarded normally. ---
            for _ in range(8):
                factory.instance.write(b"z" * (MAX_LINE_BYTES + 64))  # > limit, no \n
            await wait_for(lambda: link.stats["oversize_dropped"] == 3, timeout=2.0)
            self.assertEqual(link.state, "connected")  # reader stayed live, no wedge
            self.assertEqual(link.stats["lines"], 3)  # no new line was enqueued
            factory.instance.write(b"\n" + line({**TELEMETRY, "seq": 4}))
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["seq"], 4)
            self.assertEqual(link.stats["oversize_dropped"], 3)
            self.assertEqual(server.stats["received"], 4)

            task.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_long_no_newline_stream_keeps_buffer_bounded(self):
        """A stream that never sends a newline keeps the internal buffer
        bounded (no unbounded accumulation) and the reader stays live —
        the oversized line is counted once and no memory grows."""
        async def scenario():
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              read_chunk_size=64,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            # Many oversize chunks, no newline anywhere.
            for _ in range(20):
                factory.instance.write(b"z" * (MAX_LINE_BYTES + 64))
            await wait_for(lambda: link.stats["oversize_dropped"] == 1, timeout=2.0)
            self.assertEqual(link.state, "connected")
            self.assertEqual(link.stats["lines"], 0)
            # A newline terminates the oversized line; a valid line after it
            # is forwarded normally.
            factory.instance.write(b"\n" + line({**TELEMETRY, "seq": 1}))
            got = []
            async def consume():
                async for l in link.lines():
                    got.append(l)
            task = asyncio.create_task(consume())
            await wait_for(lambda: len(got) == 1, timeout=2.0)
            self.assertEqual(link.stats["oversize_dropped"], 1)
            self.assertEqual(link.stats["lines"], 1)
            task.cancel()
            await link.stop()
        asyncio.run(scenario())

    def test_disconnect_reconnect_resumes_ingest(self):
        """USB removal -> reconnecting -> reattachment resumes live ingestion
        without restarting Python; the WS server stays reachable and the
        dashboard link state shows ``lost`` during the outage."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            ingest = asyncio.create_task(ingest_loop(server, link))
            watchdog = asyncio.create_task(
                # read_timeout=5.0: the idle path must not fire before the
                # unplug (the producer sends one line, then goes quiet); the
                # "lost" frame under test comes from the state transition.
                link_watchdog(server, link, read_timeout=5.0, interval=0.05))
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await recv_frame(client, timeout=5.0)  # hello

            # ESP32 reports the radio link alive (drives the state to
            # "connected", so the post-outage "lost" is a real change).
            factory.instance.write(line({"kind": "radio_status", "radio": "connected"}))
            link_frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(link_frame["type"], "link")
            self.assertEqual(link_frame["radio"], "connected")

            # A pre-outage packet arrives.
            factory.instance.write(line(TELEMETRY))
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["type"], "telemetry")

            # Unplug the device.
            factory.plugged = False
            factory.instance.close()
            await wait_for(lambda: link.state == "reconnecting")
            # Watchdog forces the dashboard link state to "lost".
            lost = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(lost["type"], "link")
            self.assertEqual(lost["radio"], "lost")

            # Reattach the device: ingestion resumes on the same server.
            factory.plugged = True
            await wait_for(lambda: link.state == "connected")
            factory.instance.write(line({**TELEMETRY, "seq": 2}))
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["type"], "telemetry")
            self.assertEqual(frame["seq"], 2)
            self.assertEqual(server.stats["received"], 3)  # radio_status + 2 telemetry
            # The client never disconnected — the WS server was not restarted.
            self.assertEqual(server.stats["clients"], 1)
            watchdog.cancel()
            ingest.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_backoff_bounded(self):
        """Reconnect attempts use bounded exponential backoff: delays start
        at reconnect_min_s, double, and are capped at reconnect_max_s."""
        async def scenario():
            factory = SerialFactory(plugged=False)
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.4)
            await link.start()
            # Device stays unplugged: opens fail and backoff grows.
            await wait_for(lambda: len(factory.open_times) >= 4, timeout=3.0)
            gaps = [b - a for a, b in zip(factory.open_times, factory.open_times[1:])]
            self.assertGreaterEqual(gaps[0], 0.1 - 0.05)
            self.assertGreaterEqual(gaps[1], 0.2 - 0.05)
            for g in gaps:
                self.assertLessEqual(g, 0.4 + 0.15, f"backoff exceeded max: {g}")
            self.assertTrue(gaps[0] <= gaps[1] + 0.05, "backoff not non-decreasing")
            self.assertGreaterEqual(link.stats["open_failures"], 4)
            # Reattach: the next open succeeds.
            factory.plugged = True
            await wait_for(lambda: link.state == "connected", timeout=3.0)
            self.assertEqual(link.stats["opens"], 1)
            await link.stop()
        asyncio.run(scenario())

    def test_queue_overflow_reported(self):
        """Queue overflow is reported (counted + logged), not silently lost."""
        async def scenario():
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory, queue_max=5,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            # No consumer: the bounded queue fills and overflow is counted.
            for i in range(10):
                factory.instance.write(line({**TELEMETRY, "seq": i}))
            await wait_for(
                lambda: link.stats["lines"] == 5 and link.stats["queue_dropped"] == 5,
                timeout=2.0,
            )
            self.assertEqual(link.stats["queue_dropped"], 5)
            self.assertEqual(link.stats["lines"], 5)
            await link.stop()
        asyncio.run(scenario())

    def test_idle_watchdog_broadcasts_lost(self):
        """No serial data for read_timeout -> the watchdog broadcasts
        ``link {radio: lost}`` once; a later ESP32 radio_status line drives
        the radio state back (watchdog only forces ``lost``)."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            task = asyncio.create_task(ingest_loop(server, link))
            watchdog = asyncio.create_task(
                link_watchdog(server, link, read_timeout=0.3, interval=0.05))
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await recv_frame(client, timeout=5.0)  # hello

            # Fresh data: radio state flows from the ESP32 radio_status line.
            factory.instance.write(line({"kind": "radio_status", "radio": "connected"}))
            link_frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(link_frame["radio"], "connected")

            # Now go idle: the watchdog forces "lost" (once, no spam).
            await wait_for(
                lambda: server.stats["broadcast"] >= 2, timeout=2.0)
            lost = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(lost["type"], "link")
            self.assertEqual(lost["radio"], "lost")
            # No further "lost" spam within a couple of watchdog cycles.
            count_before = server.stats["broadcast"]
            await asyncio.sleep(0.3)
            self.assertEqual(server.stats["broadcast"], count_before)

            # A radio_status line drives the state back.
            factory.instance.write(line({"kind": "radio_status", "radio": "degraded"}))
            back = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(back["radio"], "degraded")
            watchdog.cancel()
            task.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_missing_at_startup_stays_operational(self):
        """Serial missing at startup: the server stays operational, reports
        reconnecting, and begins ingesting when the device appears."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port, MissionState(mission="Test"))
            await server.start()
            factory = SerialFactory(plugged=False)
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "reconnecting")
            # The WS server is fully operational with no serial device.
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            hello = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(hello["type"], "hello")
            self.assertIsNone(hello["launch_ts"])  # no fabricated data
            # Device appears: ingestion begins without any restart.
            factory.plugged = True
            await wait_for(lambda: link.state == "connected")
            task = asyncio.create_task(ingest_loop(server, link))
            factory.instance.write(line(TELEMETRY))
            frame = json.loads(await recv_frame(client, timeout=5.0))
            self.assertEqual(frame["type"], "telemetry")
            task.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_new_client_receives_current_link_state(self):
        """S1 contract: on connection, deliver the contractually correct
        link state. A client that connects after radio activity receives
        hello + the current `link` frame (the hello carries no radio field)."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            task = asyncio.create_task(ingest_loop(server, link))
            # First client sees the radio state change live.
            c1 = await connect(f"ws://127.0.0.1:{port}/ws")
            await c1.recv()  # hello (no link frame: no radio state yet)
            factory.instance.write(line({"kind": "radio_status", "radio": "connected"}))
            frame = json.loads(await c1.recv())
            self.assertEqual(frame["type"], "link")
            self.assertEqual(frame["radio"], "connected")
            # A second client connecting later receives the current state.
            c2 = await connect(f"ws://127.0.0.1:{port}/ws")
            hello = json.loads(await c2.recv())
            self.assertEqual(hello["type"], "hello")
            link_frame = json.loads(await c2.recv())
            self.assertEqual(link_frame["type"], "link")
            self.assertEqual(link_frame["radio"], "connected")
            await c1.close()
            await c2.close()
            task.cancel()
            await link.stop()
            await server.stop()
        asyncio.run(scenario())

    def test_stop_closes_cleanly(self):
        """stop() closes the port, joins the reader thread, terminates the
        lines() iterator, and is idempotent."""
        async def scenario():
            factory = SerialFactory()
            link = SerialLink("fake-port", serial_class=factory,
                              reconnect_min_s=0.1, reconnect_max_s=0.5)
            await link.start()
            await wait_for(lambda: link.state == "connected")
            got = []
            async def consume():
                async for l in link.lines():
                    got.append(l)
            task = asyncio.create_task(consume())
            factory.instance.write(line(TELEMETRY))
            await wait_for(lambda: len(got) == 1)
            await link.stop()
            self.assertEqual(link.state, "disconnected")
            self.assertIsNone(link._thread)
            await asyncio.wait_for(task, timeout=1.0)  # iterator terminated
            self.assertEqual(got, [line(TELEMETRY).rstrip(b"\n")])
            await link.stop()  # idempotent
        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()
