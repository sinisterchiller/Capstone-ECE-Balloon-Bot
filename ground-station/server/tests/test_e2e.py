"""End-to-end integration test for the full server pipeline (S4 close-out).

Exercises the real production code path with no mocks of the modules under
test: scripted serial bytes -> SerialLink -> main.ingest_loop ->
WebSocketServer.feed_line -> (a) Recorder JSONL on disk and (b) a real
WebSocket client receiving the translated frames, followed by
Recorder.export() producing the dashboard-compatible historical JSON.

Uses the existing scripted serial producer pattern (FakeSerial/SerialFactory
from tests/test_serial.py) so no hardware is required. A real loopback
WebSocket server on a free port and a real websockets.asyncio client are used;
the Recorder points at a temporary directory.

Run:
    python3 -m unittest tests.test_e2e -v
"""

import asyncio
import json
import tempfile
import unittest
from pathlib import Path

from main import ingest_loop
from recorder import Recorder
from serial_link import SerialLink
from tests.test_serial import FakeSerial, SerialFactory, recv_frame
from tests.test_websocket import TELEMETRY, free_port, line, wait_for
from websocket_server import MissionState, WebSocketServer

from websockets.asyncio.client import connect


TELEMETRY_2 = {
    "kind": "telemetry", "seq": 2, "ts": "2026-10-24T00:00:01Z",
    "fix": 3, "lat": 51.1, "lon": -114.1, "alt": 200.0,
}


class TestEndToEnd(unittest.TestCase):
    """Scripted serial -> SerialLink -> ingest_loop -> feed_line ->
    WS client + Recorder JSONL -> Recorder.export."""

    def test_full_pipeline_client_and_recording(self):
        """One continuous pass through the whole production path.

        Asserts:
        * the real WS client receives the per-client ``hello`` and the
          translated ``telemetry`` frames with correct fields;
        * a session ``.jsonl`` file exists in the temp data dir and contains
          the expected validated records (including ``rx_ts`` and
          ``ground_received_at``);
        * ``Recorder.export(<stem>)`` writes a ``.json`` whose parsed shape
          matches the dashboard historical contract: top-level keys
          ``schema_version``, ``mission``, ``launch_ts``, ``launch``,
          ``telemetry``, ``events``, ``onboard_records``; telemetry sorted by
          ``seq``; no invented onboard records.

        Recording is proven to have happened (recorder stats, disk file, and
        the client-received frames all agree).
        """
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                port = free_port()

                recorder = Recorder(data_dir)
                await recorder.start()
                recorder.set_mission("Test Mission")

                state = MissionState(mission="Test Mission")
                server = WebSocketServer("127.0.0.1", port, state,
                                          recorder=recorder)
                await server.start()

                factory = SerialFactory()
                link = SerialLink("fake-port", 115200, serial_class=factory,
                                  reconnect_min_s=0.1, reconnect_max_s=0.5)
                await link.start()
                await wait_for(lambda: link.state == "connected")
                task = asyncio.create_task(ingest_loop(server, link))

                try:
                    client = await connect(f"ws://127.0.0.1:{port}/ws")
                    hello = json.loads(await recv_frame(client, timeout=5.0))
                    self.assertEqual(hello["type"], "hello")
                    self.assertEqual(hello["mission"], "Test Mission")
                    self.assertEqual(hello["schema_version"], 1)

                    # Feed two valid telemetry lines through the real serial
                    # path (no rx_ts provided -> ground receive time captured).
                    factory.instance.write(line(TELEMETRY))
                    frame1_raw = await recv_frame(client, timeout=5.0)
                    self.assertIsInstance(frame1_raw, str, "text frame")
                    frame1 = json.loads(frame1_raw)
                    self.assertEqual(frame1["type"], "telemetry")
                    self.assertEqual(frame1["seq"], TELEMETRY["seq"])
                    self.assertEqual(frame1["ts"], TELEMETRY["ts"])
                    self.assertEqual(frame1["alt"], TELEMETRY["alt"])
                    self.assertIn("rx_ts", frame1)
                    self.assertIn("ground_received_at", frame1)

                    factory.instance.write(line(TELEMETRY_2))
                    frame2_raw = await recv_frame(client, timeout=5.0)
                    self.assertIsInstance(frame2_raw, str, "text frame")
                    frame2 = json.loads(frame2_raw)
                    self.assertEqual(frame2["type"], "telemetry")
                    self.assertEqual(frame2["seq"], TELEMETRY_2["seq"])
                    self.assertIn("rx_ts", frame2)
                    self.assertIn("ground_received_at", frame2)

                    # The client received exactly the two telemetry frames.
                    self.assertEqual(server.stats["received"], 2)
                    self.assertEqual(server.stats["recorded"], 2)

                    await client.close()

                    # Recording: a session .jsonl exists with the records.
                    files = list(data_dir.glob("*.jsonl"))
                    self.assertEqual(len(files), 1)
                    session_path = files[0]
                    lines = [l for l in session_path.read_text().splitlines()
                             if l.strip()]
                    self.assertEqual(len(lines), 2)
                    records = [json.loads(l) for l in lines]
                    for rec in records:
                        self.assertEqual(rec["type"], "telemetry")
                        self.assertIn("rx_ts", rec)
                        self.assertIn("ground_received_at", rec)
                        self.assertEqual(rec["mission"], "Test Mission")
                    self.assertEqual(
                        {r["seq"] for r in records}, {1, 2})

                    # Export: dashboard-compatible historical JSON shape.
                    stem = session_path.stem
                    out = recorder.export(stem)
                    self.assertTrue(out.exists())
                    self.assertEqual(out.suffix, ".json")
                    export_data = json.loads(out.read_text())
                    for key in ("schema_version", "mission", "launch_ts",
                                "launch", "telemetry", "events",
                                "onboard_records"):
                        self.assertIn(key, export_data)
                    self.assertEqual(export_data["schema_version"], 1)
                    self.assertEqual(export_data["mission"], "Test Mission")
                    self.assertEqual(len(export_data["telemetry"]), 2)
                    self.assertEqual(export_data["events"], [])
                    # No invented onboard records.
                    self.assertEqual(export_data["onboard_records"], 0)
                    # Telemetry sorted by seq.
                    seqs = [t["seq"] for t in export_data["telemetry"]]
                    self.assertEqual(seqs, sorted(seqs))
                    self.assertEqual(seqs, [1, 2])
                finally:
                    task.cancel()
                    await link.stop()
                    await server.stop()
                    await recorder.stop()
        asyncio.run(scenario())

    def test_recording_continues_without_client(self):
        """Recording does not depend on an active WebSocket client: with
        zero clients connected, fed serial lines are still written to the
        session JSONL and exportable."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                port = free_port()

                recorder = Recorder(data_dir)
                await recorder.start()
                recorder.set_mission("NoClient")

                server = WebSocketServer("127.0.0.1", port,
                                          MissionState(mission="NoClient"),
                                          recorder=recorder)
                await server.start()

                factory = SerialFactory()
                link = SerialLink("fake-port", 115200, serial_class=factory,
                                  reconnect_min_s=0.1, reconnect_max_s=0.5)
                await link.start()
                await wait_for(lambda: link.state == "connected")
                task = asyncio.create_task(ingest_loop(server, link))

                try:
                    # No client is connected (server.stats["clients"] == 0).
                    self.assertEqual(server.stats["clients"], 0)
                    factory.instance.write(line(TELEMETRY))
                    factory.instance.write(line(TELEMETRY_2))
                    await wait_for(lambda: server.stats["received"] == 2,
                                   timeout=5.0)
                    self.assertEqual(server.stats["recorded"], 2)

                    # Both records are on disk despite zero clients.
                    files = list(data_dir.glob("*.jsonl"))
                    self.assertEqual(len(files), 1)
                    lines = [l for l in files[0].read_text().splitlines()
                             if l.strip()]
                    self.assertEqual(len(lines), 2)

                    # Exportable with the dashboard-compatible shape.
                    out = recorder.export(files[0].stem)
                    export_data = json.loads(out.read_text())
                    self.assertEqual(len(export_data["telemetry"]), 2)
                    self.assertEqual(export_data["onboard_records"], 0)
                finally:
                    task.cancel()
                    await link.stop()
                    await server.stop()
                    await recorder.stop()
        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()
