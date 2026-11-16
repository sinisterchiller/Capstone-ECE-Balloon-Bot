"""Tests for websocket_server.py — accept, per-client hello, broadcast,
multi-client fan-out, disconnect handling, and invalid-input rejection (S2).

Uses a real loopback WebSocket server on a free port and real dashboard-style
clients (websockets.asyncio.client). Run:
    python3 -m unittest tests.test_websocket -v
"""

import asyncio
import json
import logging
import socket
import unittest

from websockets.asyncio.client import connect

from protocol import encode_ws_message
from websocket_server import MissionState, WebSocketServer


def line(obj) -> bytes:
    return (json.dumps(obj) + "\n").encode("utf-8")


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


async def wait_for(predicate, timeout: float = 2.0) -> None:
    """Wait until ``predicate()`` is true (the server processes events async)."""
    loop = asyncio.get_event_loop()
    deadline = loop.time() + timeout
    while loop.time() < deadline:
        if predicate():
            return
        await asyncio.sleep(0.05)
    raise AssertionError(f"timed out waiting for: {predicate}")


class StalledClient:
    """A mock client whose send() never completes (simulates backpressure stall)."""

    def __init__(self):
        self.sent = []

    async def send(self, data):
        self.sent.append(data)
        await asyncio.Event().wait()  # stall forever

    async def close(self):
        pass


TELEMETRY = {
    "kind": "telemetry", "seq": 1, "ts": "2026-10-24T00:00:00Z",
    "fix": 3, "lat": 51.0, "lon": -114.0, "alt": 100.0,
}


class TestWebSocketServer(unittest.TestCase):
    def test_connect_receives_hello(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port, MissionState(mission="Test"))
            await server.start()
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            hello = json.loads(await client.recv())
            self.assertEqual(hello["type"], "hello")
            self.assertEqual(hello["mission"], "Test")
            self.assertEqual(hello["schema_version"], 1)
            await client.close()
            await server.stop()
        asyncio.run(scenario())

    def test_frames_are_text_not_binary(self):
        """Regression: every frame the server sends must be a *text* WS frame
        (str on the wire), not binary (bytes). The dashboard's JSON.parse
        expects text; a binary frame arrives as a Blob and is dropped. The
        websockets client returns str for text frames and bytes for binary
        frames, so assert isinstance(..., str) on the raw recv()."""
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port, MissionState(mission="Test"))
            await server.start()
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            hello_raw = await client.recv()
            self.assertIsInstance(hello_raw, str, "hello must be a text frame")
            self.assertEqual(json.loads(hello_raw)["type"], "hello")
            # Feed a telemetry line; the broadcast frame must also be text.
            await server.feed_line(line(TELEMETRY))
            frame_raw = await client.recv()
            self.assertIsInstance(frame_raw, str, "telemetry must be a text frame")
            self.assertEqual(json.loads(frame_raw)["type"], "telemetry")
            await client.close()
            await server.stop()
        asyncio.run(scenario())

    def test_hello_unknown_state_when_no_data(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port, MissionState(mission="Test"))
            await server.start()
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            hello = json.loads(await client.recv())
            # No mission data yet -> unknown/disconnected, no fabricated measurements.
            self.assertIsNone(hello["launch_ts"])
            self.assertIsNone(hello["launch"])
            await client.close()
            await server.stop()
        asyncio.run(scenario())

    def test_hello_carries_state_after_data(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port, MissionState(mission="Test"))
            await server.start()
            c1 = await connect(f"ws://127.0.0.1:{port}/ws")
            await c1.recv()  # hello (no data yet)
            await server.feed_line(line(TELEMETRY))
            await c1.recv()  # telemetry
            # A second client connecting after data arrives gets state in its hello.
            c2 = await connect(f"ws://127.0.0.1:{port}/ws")
            hello = json.loads(await c2.recv())
            self.assertEqual(hello["launch_ts"], "2026-10-24T00:00:00Z")
            self.assertIsNotNone(hello["launch"])
            await c1.close()
            await c2.close()
            await server.stop()
        asyncio.run(scenario())

    def test_fixture_feeds_frame(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port, MissionState(mission="Test"))
            await server.start()
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await client.recv()  # hello
            ok = await server.feed_line(line(TELEMETRY))
            self.assertTrue(ok)
            frame = json.loads(await client.recv())
            self.assertEqual(frame["type"], "telemetry")
            self.assertEqual(frame["seq"], 1)
            self.assertEqual(frame["alt"], 100.0)
            self.assertEqual(server.stats["received"], 1)
            await client.close()
            await server.stop()
        asyncio.run(scenario())

    def test_invalid_rejected_no_false_telemetry(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await client.recv()  # hello
            # Malformed line: rejected, no frame broadcast.
            self.assertFalse(await server.feed_line(b"not json\n"))
            # Structurally invalid (bad seq): rejected.
            self.assertFalse(await server.feed_line(line(
                {"kind": "telemetry", "seq": -1, "ts": "2026-10-24T00:00:00Z"})))
            self.assertEqual(server.stats["rejected"], 2)
            self.assertEqual(server.stats["received"], 0)
            await client.close()
            await server.stop()
        asyncio.run(scenario())

    def test_multi_client_fanout(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            c1 = await connect(f"ws://127.0.0.1:{port}/ws")
            c2 = await connect(f"ws://127.0.0.1:{port}/ws")
            await c1.recv(); await c2.recv()  # hellos
            await server.feed_line(line(TELEMETRY))
            f1 = json.loads(await c1.recv())
            f2 = json.loads(await c2.recv())
            self.assertEqual(f1["type"], "telemetry")
            self.assertEqual(f2["type"], "telemetry")
            self.assertEqual(f1["seq"], f2["seq"])
            self.assertEqual(server.stats["clients"], 2)
            await c1.close(); await c2.close()
            await server.stop()
        asyncio.run(scenario())

    def test_disconnect_does_not_break_ingest(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            c1 = await connect(f"ws://127.0.0.1:{port}/ws")
            await c1.recv()  # hello
            await c1.close()  # disconnect
            await wait_for(lambda: server.stats["clients"] == 0)
            # Ingestion continues after the only client disconnects.
            self.assertTrue(await server.feed_line(line(TELEMETRY)))
            self.assertEqual(server.stats["received"], 1)
            await server.stop()
        asyncio.run(scenario())

    def test_link_and_ack_forwarded(self):
        async def scenario():
            port = free_port()
            server = WebSocketServer("127.0.0.1", port)
            await server.start()
            client = await connect(f"ws://127.0.0.1:{port}/ws")
            await client.recv()  # hello
            await server.feed_line(line({"kind": "radio_status", "radio": "connected"}))
            link = json.loads(await client.recv())
            self.assertEqual(link["type"], "link")
            self.assertEqual(link["radio"], "connected")
            await server.feed_line(line({"kind": "radio_ack", "seq": 1, "ack": "acked"}))
            ack = json.loads(await client.recv())
            self.assertEqual(ack["type"], "ack")
            self.assertEqual(ack["seq"], 1)
            self.assertEqual(ack["ack"], "acked")
            await client.close()
            await server.stop()
        asyncio.run(scenario())

    def test_stalled_client_cannot_block_ingest(self):
        async def scenario():
            port = free_port()
            # Small send timeout so the test is fast.
            server = WebSocketServer("127.0.0.1", port, send_timeout=0.3)
            await server.start()
            # A stalled client whose send() never completes.
            stalled = StalledClient()
            server._clients.add(stalled)
            server.stats["clients"] = 1

            # Broadcast must complete within the timeout (not block indefinitely),
            # and the stalled client must be dropped.
            loop = asyncio.get_event_loop()
            start = loop.time()
            await server.broadcast(encode_ws_message({"type": "link", "radio": "lost"}))
            elapsed = loop.time() - start
            self.assertLessEqual(elapsed, 0.6,
                                 "broadcast blocked longer than the send timeout")
            self.assertNotIn(stalled, server._clients)
            self.assertEqual(server.stats["dropped"], 1)

            # Subsequent ingestion is not blocked by the dropped stalled client.
            self.assertTrue(await server.feed_line(line(TELEMETRY)))
            self.assertEqual(server.stats["received"], 1)
            await server.stop()
        asyncio.run(scenario())

    def test_reject_logging_visible_at_default_level(self):
        """Rejections are logged at INFO (visible at the default log level)
        with a bounded, payload-free message — never dumping payload contents
        or flooding the console."""
        async def scenario():
            server = WebSocketServer("127.0.0.1", free_port())
            await server.start()
            records = []

            class _Handler(logging.Handler):
                def emit(self, record):
                    records.append(self.format(record))

            handler = _Handler()
            handler.setLevel(logging.INFO)
            logger = logging.getLogger("websocket_server")
            logger.addHandler(handler)
            logger.setLevel(logging.INFO)

            try:
                # First failure is logged immediately at INFO.
                self.assertFalse(await server.feed_line(b"not json\n"))
                # Repeated rejects within the window do NOT produce one log per line.
                for _ in range(20):
                    self.assertFalse(await server.feed_line(b"not json\n"))
            finally:
                logger.removeHandler(handler)

            self.assertEqual(server.stats["rejected"], 21)
            # Exactly one first-failure log line; no per-line flood.
            self.assertEqual(len(records), 1)
            rec = records[0]
            self.assertIn("rejected line", rec)
            self.assertIn("bytes", rec)
            # Payload contents must never leak into the log.
            self.assertNotIn("not json", rec)
        asyncio.run(scenario())

    def test_reject_logging_summary_reports_skipped_count(self):
        """After a quiet window, a summary of the skipped rejects is logged
        (with the count), still at INFO, still payload-free."""
        async def scenario():
            server = WebSocketServer("127.0.0.1", free_port())
            await server.start()
            records = []

            class _Handler(logging.Handler):
                def emit(self, record):
                    records.append(self.format(record))

            handler = _Handler()
            handler.setLevel(logging.INFO)
            logger = logging.getLogger("websocket_server")
            logger.addHandler(handler)
            logger.setLevel(logging.INFO)

            import websocket_server as ws_mod
            old = ws_mod.REJECT_LOG_WINDOW_S
            ws_mod.REJECT_LOG_WINDOW_S = 0.05  # shrink the window for the test
            try:
                # First + a burst of rejects.
                self.assertFalse(await server.feed_line(b"not json\n"))
                for _ in range(4):
                    self.assertFalse(await server.feed_line(b"not json\n"))
                self.assertEqual(len(records), 1)  # first failure only

                # Let the window elapse.
                await asyncio.sleep(0.15)

                # A new rejection after the window logs a summary (count) + this one.
                self.assertFalse(await server.feed_line(b"not json\n"))
                self.assertEqual(server.stats["rejected"], 6)
                # Summary line + the new first-failure line = 2 more records (total 3).
                self.assertEqual(len(records), 3)
                summary = records[1]
                self.assertIn("rejected 5 lines", summary)
                self.assertNotIn("not json", summary)
                self.assertNotIn("not json", records[2])
            finally:
                ws_mod.REJECT_LOG_WINDOW_S = old
                logger.removeHandler(handler)
            await server.stop()
        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()
