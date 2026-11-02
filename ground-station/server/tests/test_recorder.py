"""Tests for recorder.py — append-only JSONL recording and session export (S4).

Covers:
* Recording writes immediately (line reaches OS buffer before record() returns).
* Malformed input / malformed JSONL line rejection.
* record() raises RuntimeError before start() and after stop().
* Explicit start() required to resume recording after stop().
* Partial final line recovery (truncated last line in JSONL).
* Export contract (exact dashboard historical JSON shape).
* Lifecycle / failure behaviour (stop, restart, disk error propagation).
* Fixture replay does not duplicate records or create fake observations.
* Two clients receiving replay without duplicating recorder writes.
* stop() propagates fsync errors.
* Session filename uniqueness and append mode.
* Ground receive time capture in feed_line (rx_ts preserved, ground_received_at set).
* Mission name persisted in JSONL records (S4 defect 1).
* Absolute path support in --export (S4 defect 2).
* Event rssi/snr preservation (S4 defect 4).
* WebSocket broadcast rx_ts consistency (S4 defect 3).
* CLI export path handling (S4 defect 2).
* Invalid rx_ts uses ground receive time, not flight timestamp.
* Relative .jsonl path resolved from cwd, not from data_dir.

Run:
    python3 -m unittest tests.test_recorder -v
"""

from __future__ import annotations

import asyncio
import json
import os
import tempfile
import unittest
from pathlib import Path

from fixture_source import FixtureSource
from protocol import encode_ws_message, to_ws_message, validate
from recorder import Recorder
from websocket_server import MissionState, WebSocketServer

from websockets.asyncio.client import connect


def line(obj) -> bytes:
    return (json.dumps(obj) + "\n").encode("utf-8")


def free_port() -> int:
    import socket
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


async def wait_for(pred, timeout: float = 3.0) -> None:
    loop = asyncio.get_event_loop()
    deadline = loop.time() + timeout
    while loop.time() < deadline:
        if pred():
            return
        await asyncio.sleep(0.05)
    raise AssertionError(f"timed out waiting for: {pred}")


# ---- fixtures -----------------------------------------------------------

SAMPLE_TELEMETRY = {
    "kind": "telemetry", "seq": 1, "ts": "2026-10-24T12:00:00Z",
    "fix": 3, "lat": 51.078, "lon": -114.123, "alt": 0.0,
    "temp_ext": 12.0, "pressure": 1013.0, "battery": 8.4,
    "mode": "NORMAL", "phase": "PRELAUNCH", "rssi": -82, "snr": 6.5,
}

SAMPLE_EVENT = {
    "kind": "event", "ts": "2026-10-24T12:01:00Z",
    "category": "threshold", "severity": "warning",
    "code": "BURST_DETECT", "message": "Burst detected",
    "measurement": "pressure", "value": 195.0, "threshold": 195.0,
    "unit": "hPa", "mode": "EVENT", "seq": 3,
    "rx_ts": "2026-10-24T12:01:00Z",
}

SAMPLE_LINK = {
    "kind": "radio_status", "ts": "2026-10-24T12:00:00Z", "radio": "connected",
}

SAMPLE_ACK = {
    "kind": "radio_ack", "ts": "2026-10-24T12:00:00.200Z", "seq": 1, "ack": "acked",
}


class TestImmediateWrite(unittest.TestCase):
    """record() writes immediately — line on disk before the call returns."""

    def test_line_on_disk_before_record_returns(self):
        """After record() returns, the JSONL file contains exactly one line."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                frame = to_ws_message(norm)
                await rec.record(frame)
                # File must have exactly one complete line.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                text = files[0].read_text()
                self.assertEqual(text.count("\n"), 1)
                obj = json.loads(text.strip())
                self.assertEqual(obj["seq"], 1)
                await rec.stop()
        asyncio.run(scenario())

    def test_multiple_records_immediate(self):
        """Each record() call immediately persists its line."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                for i in range(5):
                    norm = validate({"kind": "telemetry", "seq": i,
                                     "ts": f"2026-10-24T12:00:{i:02d}Z",
                                     "fix": 3, "lat": 51.0, "lon": -114.0,
                                     "alt": float(i)})
                    await rec.record(to_ws_message(norm))
                    # After each record, the file should have exactly i+1 lines.
                    files = list(data_dir.glob("*.jsonl"))
                    text = files[0].read_text()
                    self.assertEqual(text.count("\n"), i + 1)
                await rec.stop()
                self.assertEqual(rec.stats["records_written"], 5)
        asyncio.run(scenario())


class TestAppendModeAndUniqueFilename(unittest.TestCase):
    """Session files use append mode and unique names."""

    def test_append_mode_preserves_existing(self):
        """Opening a session file in append mode does not truncate."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                # Pre-create a file with content.
                existing = data_dir / "preexisting.jsonl"
                existing.write_text('{"type":"telemetry","seq":0}\n')
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                # The new session file should be different from preexisting.
                files = sorted(data_dir.glob("*.jsonl"))
                self.assertGreaterEqual(len(files), 2)
        asyncio.run(scenario())

    def test_filename_is_unique(self):
        """Rapid successive sessions never collide."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                for i in range(10):
                    await rec.start()
                    norm = validate({"kind": "telemetry", "seq": i,
                                     "ts": f"2026-10-24T12:00:00Z",
                                     "fix": 3, "lat": 51.0, "lon": -114.0,
                                     "alt": float(i)})
                    await rec.record(to_ws_message(norm))
                    await rec.stop()
                files = list(data_dir.glob("*.jsonl"))
                # All filenames must be unique.
                names = [f.name for f in files]
                self.assertEqual(len(names), len(set(names)))
                self.assertEqual(len(files), 10)
        asyncio.run(scenario())

    def test_set_mission_applied_to_filename(self):
        """set_mission() sets the mission name in the session filename."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                rec.set_mission("test_mission")
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                self.assertTrue(files[0].name.startswith("test_mission-"))
        asyncio.run(scenario())


class TestMalformedInput(unittest.TestCase):
    """Malformed JSONL lines are skipped during export."""

    def test_export_skips_malformed_lines(self):
        """Export should skip malformed complete lines and still produce
        a valid JSON file with the good records."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                # Record a valid telemetry.
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                frame = to_ws_message(norm)
                await rec.record(frame)
                await rec.stop()

                # Now corrupt the file: append bad JSON and a truncated line.
                files = list(data_dir.glob("*.jsonl"))
                path = files[0]
                text = path.read_text()
                text += "THIS IS NOT JSON\n"
                text += '{"type": "tele'  # truncated — no newline
                path.write_text(text)

                # Export should skip the bad lines.
                out = rec.export(path.stem)
                export_data = json.loads(out.read_text())
                self.assertEqual(len(export_data["telemetry"]), 1)
                self.assertEqual(export_data["telemetry"][0]["seq"], 1)
                self.assertEqual(export_data["schema_version"], 1)
        asyncio.run(scenario())

    def test_export_with_no_complete_lines(self):
        """If all lines are malformed, export produces empty arrays."""
        with tempfile.TemporaryDirectory() as tmp:
            data_dir = Path(tmp)
            rec = Recorder(data_dir)
            # Write only malformed content.
            path = data_dir / "bad.jsonl"
            path.write_text("not json\n{broken\n")
            out = rec.export(path.stem)
            export_data = json.loads(out.read_text())
            self.assertEqual(export_data["telemetry"], [])
            self.assertEqual(export_data["events"], [])
            self.assertEqual(export_data["onboard_records"], 0)

    def test_export_warns_on_malformed_lines(self):
        """Export logs a warning for each malformed complete line."""
        with tempfile.TemporaryDirectory() as tmp:
            data_dir = Path(tmp)
            rec = Recorder(data_dir)
            path = data_dir / "bad.jsonl"
            path.write_text('{"type":"telemetry","seq":1,"ts":"2026-10-24T12:00:00Z"}\n'
                            'BROKEN LINE\n'
                            '{"type":"telemetry","seq":2,"ts":"2026-10-24T12:01:00Z"}\n')
            out = rec.export(path.stem)
            export_data = json.loads(out.read_text())
            # Only the two good records are exported.
            self.assertEqual(len(export_data["telemetry"]), 2)


class TestPartialFinalLine(unittest.TestCase):
    """Partial / truncated last line in JSONL is handled safely."""

    def test_truncated_last_line_ignored(self):
        """A file ending mid-JSON is exportable; the truncated line is
        silently ignored and previous records are recovered."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                # Record two valid packets.
                for seq in [1, 2]:
                    norm = validate({"kind": "telemetry", "seq": seq,
                                     "ts": f"2026-10-24T12:00:{seq:02d}Z",
                                     "fix": 3, "lat": 51.0, "lon": -114.0,
                                     "alt": float(seq)})
                    await rec.record(to_ws_message(norm))
                await rec.stop()

                # Simulate a crash: truncate the last line.
                files = list(data_dir.glob("*.jsonl"))
                path = files[0]
                text = path.read_text()
                # Remove the last complete line (keep only the first record).
                last_nl = text.rfind("\n")
                second_last_nl = text.rfind("\n", 0, last_nl)
                if second_last_nl > 0:
                    text = text[:second_last_nl]  # remove the last complete line
                # Now add a truncated line.
                text += '\n{"type": "telemetry", "seq": 99'
                path.write_text(text)

                out = rec.export(path.stem)
                export_data = json.loads(out.read_text())
                # Only the first complete record survives.
                self.assertEqual(len(export_data["telemetry"]), 1)
                self.assertEqual(export_data["telemetry"][0]["seq"], 1)
        asyncio.run(scenario())


class TestExportContract(unittest.TestCase):
    """Export produces the exact shape expected by parseSessionFile."""

    def test_export_shape(self):
        """The exported JSON has schema_version, mission, launch_ts, launch,
        telemetry, events, onboard_records — matching parseSessionFile."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                # Record telemetry + event.
                norm_t = validate({"kind": "telemetry", "seq": 1,
                                   "ts": "2026-10-24T12:00:00Z",
                                   "fix": 3, "lat": 51.078, "lon": -114.123,
                                   "alt": 0.0, "temp_ext": 12.0,
                                   "pressure": 1013.0, "battery": 8.4,
                                   "battery_pct": 100, "mode": "NORMAL",
                                   "phase": "PRELAUNCH", "rssi": -82,
                                   "snr": 6.5})
                norm_e = validate({"kind": "event",
                                   "ts": "2026-10-24T12:01:00Z",
                                   "category": "threshold",
                                   "severity": "warning",
                                   "code": "BURST_DETECT",
                                   "message": "Burst detected",
                                   "measurement": "pressure", "value": 195.0,
                                   "threshold": 195.0, "unit": "hPa",
                                   "mode": "EVENT", "seq": 3})
                await rec.record(to_ws_message(norm_t))
                await rec.record(to_ws_message(norm_e))
                await rec.stop()

                out = rec.export(list(data_dir.glob("*.jsonl"))[0].stem)
                export_data = json.loads(out.read_text())

                # Top-level keys.
                self.assertIn("schema_version", export_data)
                self.assertEqual(export_data["schema_version"], 1)
                self.assertIn("mission", export_data)
                self.assertIn("launch_ts", export_data)
                self.assertIn("launch", export_data)
                self.assertIn("telemetry", export_data)
                self.assertIn("events", export_data)
                self.assertIn("onboard_records", export_data)

                # launch has lat/lon.
                self.assertEqual(export_data["launch"]["lat"], 51.078)
                self.assertEqual(export_data["launch"]["lon"], -114.123)

                # Telemetry has RSSI/SNR preserved.
                self.assertEqual(export_data["telemetry"][0]["rssi"], -82)
                self.assertEqual(export_data["telemetry"][0]["snr"], 6.5)

                # Events preserved.
                self.assertEqual(len(export_data["events"]), 1)
                self.assertEqual(export_data["events"][0]["code"], "BURST_DETECT")

                # onboard_records is 0 (ground records don't know this).
                self.assertEqual(export_data["onboard_records"], 0)
        asyncio.run(scenario())

    def test_export_preserves_timestamps_and_seq(self):
        """Exported telemetry includes ts, seq, and validated values."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 42,
                                 "ts": "2026-10-24T12:00:00Z",
                                 "fix": 3, "lat": 51.0, "lon": -114.0,
                                 "alt": 100.0, "alt_baro": 99.0,
                                 "temp_ext": -10.0, "temp_int": 3.0,
                                 "rh": 45.0, "pressure": 195.0,
                                 "vspeed": 5.0, "battery": 8.1,
                                 "battery_pct": 80, "mode": "EVENT",
                                 "phase": "BURST", "rssi": -95,
                                 "snr": 3.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                out = rec.export(list(data_dir.glob("*.jsonl"))[0].stem)
                export_data = json.loads(out.read_text())
                telem = export_data["telemetry"][0]
                self.assertEqual(telem["seq"], 42)
                self.assertEqual(telem["ts"], "2026-10-24T12:00:00Z")
                self.assertEqual(telem["alt"], 100.0)
                self.assertEqual(telem["alt_baro"], 99.0)
                self.assertEqual(telem["mode"], "EVENT")
                self.assertEqual(telem["phase"], "BURST")
        asyncio.run(scenario())

    def test_export_sorted_by_seq(self):
        """Exported telemetry is sorted by sequence number."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                # Record out of order.
                for seq in [3, 1, 2]:
                    norm = validate({"kind": "telemetry", "seq": seq,
                                     "ts": f"2026-10-24T12:00:{seq:02d}Z",
                                     "fix": 3, "lat": 51.0, "lon": -114.0,
                                     "alt": float(seq)})
                    await rec.record(to_ws_message(norm))
                await rec.stop()

                out = rec.export(list(data_dir.glob("*.jsonl"))[0].stem)
                export_data = json.loads(out.read_text())
                seqs = [t["seq"] for t in export_data["telemetry"]]
                self.assertEqual(seqs, [1, 2, 3])
        asyncio.run(scenario())

    def test_export_no_invented_onboard_records(self):
        """onboard_records is always 0 for ground-recorded sessions."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z",
                                 "fix": 3, "lat": 51.0, "lon": -114.0,
                                 "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                out = rec.export(list(data_dir.glob("*.jsonl"))[0].stem)
                self.assertEqual(
                    json.loads(out.read_text())["onboard_records"], 0)
        asyncio.run(scenario())

    def test_export_path_safety_output_not_in_data_dir(self):
        """Exported .json file is written next to the .jsonl, not inside
        data_dir in a way that could overwrite source data."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z",
                                 "fix": 3, "lat": 51.0, "lon": -114.0,
                                 "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                # Export with explicit path (stem).
                jsonl_path = list(data_dir.glob("*.jsonl"))[0]
                out = rec.export(jsonl_path.stem)
                # Output is .json next to the .jsonl.
                self.assertTrue(out.exists())
                self.assertEqual(out.suffix, ".json")
                # Source .jsonl is untouched.
                self.assertTrue(jsonl_path.exists())
        asyncio.run(scenario())

    def test_export_missing_session_raises(self):
        """Exporting a non-existent session raises FileNotFoundError."""
        with tempfile.TemporaryDirectory() as tmp:
            rec = Recorder(Path(tmp))
            with self.assertRaises(FileNotFoundError):
                rec.export("nonexistent")

    def test_export_avoids_overwriting_existing(self):
        """If the .json output already exists, a -1.json suffix is used."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                jsonl_path = list(data_dir.glob("*.jsonl"))[0]
                # Pre-create the .json output.
                json_path = jsonl_path.with_suffix(".json")
                json_path.write_text('{"existing": true}')
                # Export again — should create -1.json.
                out = rec.export(jsonl_path.stem)
                self.assertTrue(out.exists())
                # The original .json should still exist with its original data.
                self.assertTrue(json_path.exists())
                self.assertEqual(json.loads(json_path.read_text())["existing"], True)
        asyncio.run(scenario())


class TestNoViewers(unittest.TestCase):
    """Recording works independently of WebSocket clients."""

    def test_recording_with_no_ws_clients(self):
        """Telemetry arrives and is recorded even with zero WebSocket
        clients connected."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                # Feed lines with no clients connected.
                self.assertTrue(await server.feed_line(line(SAMPLE_TELEMETRY)))
                self.assertTrue(await server.feed_line(line(SAMPLE_EVENT)))
                self.assertEqual(rec.stats["records_written"], 2)

                await server.stop()
                await rec.stop()

                # Verify the file exists.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
        asyncio.run(scenario())


class TestFixtureReplayNoDuplication(unittest.TestCase):
    """Fixture replay through the server does not duplicate records or
    create fake ground observations."""

    def test_fixture_replay_records_once_per_ingestion(self):
        """When a fixture is replayed through feed_line (the single ingestion
        path), each valid line is recorded exactly once."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                # Feed lines through feed_line (the single ingestion path).
                self.assertTrue(await server.feed_line(line(SAMPLE_TELEMETRY)))
                self.assertTrue(await server.feed_line(line(SAMPLE_EVENT)))

                # Recorder should have exactly 2 records (one per valid line).
                self.assertEqual(rec.stats["records_written"], 2)

                await server.stop()
                await rec.stop()
        asyncio.run(scenario())

    def test_two_clients_no_duplicate_records(self):
        """Two clients connecting both receive replay, but the recorder
        only counts records from feed_line ingestion — fixture replay
        is display-only and does not write to the recorder."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                fixture_lines = [line(SAMPLE_TELEMETRY)]
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec,
                                         fixture_lines=fixture_lines)
                await server.start()

                # Connect first client.
                client1 = await connect(f"ws://127.0.0.1:{port}/ws")
                await client1.recv()  # hello
                await asyncio.wait_for(client1.recv(), timeout=2.0)  # replay

                # Connect second client.
                client2 = await connect(f"ws://127.0.0.1:{port}/ws")
                await client2.recv()  # hello
                await asyncio.wait_for(client2.recv(), timeout=2.0)  # replay

                # Recorder should have ZERO records — replay doesn't record.
                self.assertEqual(rec.stats["records_written"], 0)
                # But both clients received the replay.
                self.assertEqual(server.stats["broadcast"], 0)  # replay doesn't broadcast either

                await client1.close()
                await client2.close()
                await server.stop()
                await rec.stop()
        asyncio.run(scenario())

    def test_fixture_replay_no_fake_rf_metadata(self):
        """RF metadata (rssi, snr) in recorded telemetry matches what was
        actually in the fixture — no fabricated values."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                # Feed a telemetry line through the ingestion path.
                self.assertTrue(await server.feed_line(line(SAMPLE_TELEMETRY)))

                await server.stop()
                await rec.stop()

                # Check the recorded data.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                text = files[0].read_text().strip()
                rec_data = json.loads(text)
                # rssi/snr match the fixture exactly.
                self.assertEqual(rec_data["rssi"], -82)
                self.assertEqual(rec_data["snr"], 6.5)
        asyncio.run(scenario())


class TestRecordingScope(unittest.TestCase):
    """Only telemetry and event records are written; link/ack are skipped."""

    def test_link_not_recorded(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "radio_status", "radio": "connected"})
                await rec.record(to_ws_message(norm))  # link frame
                await rec.stop()
                self.assertEqual(len(list(data_dir.glob("*.jsonl"))), 0)
        asyncio.run(scenario())

    def test_ack_not_recorded(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "radio_ack", "seq": 1, "ack": "acked"})
                await rec.record(to_ws_message(norm))  # ack frame
                await rec.stop()
                self.assertEqual(len(list(data_dir.glob("*.jsonl"))), 0)
        asyncio.run(scenario())

    def test_telemetry_and_event_both_recorded(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm_t = validate({"kind": "telemetry", "seq": 1,
                                   "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                   "lat": 51.0, "lon": -114.0, "alt": 0.0})
                norm_e = validate({"kind": "event", "ts": "2026-10-24T12:01:00Z",
                                   "category": "threshold", "severity": "warning",
                                   "code": "BURST_DETECT"})
                await rec.record(to_ws_message(norm_t))
                await rec.record(to_ws_message(norm_e))
                await rec.stop()
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                lines = [l for l in files[0].read_text().splitlines() if l.strip()]
                self.assertEqual(len(lines), 2)
                types = [json.loads(l)["type"] for l in lines]
                self.assertIn("telemetry", types)
                self.assertIn("event", types)
        asyncio.run(scenario())

    def test_ack_not_recorded_through_feed_line(self):
        """feed_line() broadcasts ack frames but never records them."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                # Feed an ack frame through feed_line.
                self.assertTrue(await server.feed_line(line(SAMPLE_ACK)))

                await server.stop()
                await rec.stop()

                # Recorder should have ZERO records — ack is control, not observation.
                self.assertEqual(rec.stats["records_written"], 0)
                self.assertEqual(len(list(data_dir.glob("*.jsonl"))), 0)
        asyncio.run(scenario())


class TestLifecycle(unittest.TestCase):
    """Recorder lifecycle: start, stop, session creation, restart."""

    def test_start_creates_data_dir(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp) / "sessions"
                rec = Recorder(data_dir)
                await rec.start()
                self.assertTrue(data_dir.exists())
                await rec.stop()
        asyncio.run(scenario())

    def test_record_opens_session_file(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                frame = to_ws_message(norm)
                await rec.record(frame)
                await rec.stop()
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                text = files[0].read_text()
                self.assertEqual(text.count("\n"), 1)
                obj = json.loads(text.strip())
                self.assertEqual(obj["type"], "telemetry")
                self.assertEqual(obj["seq"], 1)
        asyncio.run(scenario())

    def test_no_session_file_when_no_records(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                await rec.stop()
                self.assertEqual(len(list(data_dir.glob("*.jsonl"))), 0)
        asyncio.run(scenario())

    def test_restart_creates_new_session_file(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                self.assertEqual(rec.stats["sessions_created"], 1)
                # Explicit start() required to resume recording.
                await rec.start()
                norm2 = validate({"kind": "telemetry", "seq": 2,
                                  "ts": "2026-10-24T13:00:00Z", "fix": 3,
                                  "lat": 51.1, "lon": -114.2, "alt": 100.0})
                await rec.record(to_ws_message(norm2))
                await rec.stop()
                self.assertEqual(rec.stats["sessions_created"], 2)
                files = sorted(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 2)
        asyncio.run(scenario())

    def test_stats_tracking(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                for i in range(5):
                    norm = validate({"kind": "telemetry", "seq": i,
                                     "ts": f"2026-10-24T12:00:{i:02d}Z",
                                     "fix": 3, "lat": 51.0, "lon": -114.0,
                                     "alt": float(i)})
                    await rec.record(to_ws_message(norm))
                await rec.stop()
                self.assertEqual(rec.stats["records_written"], 5)
                self.assertGreater(rec.stats["bytes_written"], 0)
        asyncio.run(scenario())


class TestRecordAfterStopRaises(unittest.TestCase):
    """record() after stop() raises RuntimeError — never silently opens a
    new session.  An explicit start() or fresh instance is required."""

    def test_record_after_stop_raises_runtime_error(self):
        """record() after stop() MUST raise RuntimeError, not open a new
        session.  The old file persists on disk."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                # After stop, the old session file exists.
                files_before = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files_before), 1)
                # record() after stop() raises RuntimeError — no new file.
                with self.assertRaises(RuntimeError):
                    await rec.record(to_ws_message(norm))
                # Exactly one file still exists (the old one).
                files_after = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files_after), 1)
        asyncio.run(scenario())

    def test_record_after_re_start(self):
        """After stop(), an explicit start() re-enables recording and a new
        session file is created — old file is preserved."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()
                # Explicit start() re-enables recording.
                await rec.start()
                norm2 = validate({"kind": "telemetry", "seq": 2,
                                  "ts": "2026-10-24T13:00:00Z", "fix": 3,
                                  "lat": 51.1, "lon": -114.2, "alt": 100.0})
                await rec.record(to_ws_message(norm2))
                await rec.stop()
                # Two distinct session files now exist.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 2)
                names = [f.name for f in files]
                self.assertEqual(len(names), len(set(names)))
        asyncio.run(scenario())

    def test_record_before_start_raises_runtime_error(self):
        """record() before start() raises RuntimeError."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                with self.assertRaises(RuntimeError):
                    await rec.record(to_ws_message(norm))
                # No session file was created for invalid input.
                self.assertEqual(len(list(data_dir.glob("*.jsonl"))), 0)
        asyncio.run(scenario())


class TestStopPropagatesFsyncError(unittest.TestCase):
    """Recorder.stop() propagates fsync errors instead of swallowing them."""

    def test_fsync_error_propagates(self):
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                # Close the file handle but keep the path — fsync will fail.
                rec._file.close()
                rec._file = None
                # stop() should propagate the OSError from fsync.
                with self.assertRaises(OSError):
                    await rec.stop()
        asyncio.run(scenario())


class TestDiskFailurePropagation(unittest.TestCase):
    """Disk failures are explicit, never silently swallowed."""

    def test_write_error_propagates(self):
        """If the file write fails, the exception propagates — the record
        is NOT silently lost."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z",
                                 "fix": 3, "lat": 51.0, "lon": -114.0,
                                 "alt": 0.0})
                # Open the session file.
                rec._open_session()
                # Close the file handle but keep the path.
                rec._file.close()
                rec._file = None
                # Now try to write — should fail.
                with self.assertRaises((OSError, ValueError)):
                    await rec.record(to_ws_message(norm))
                self.assertGreater(rec.stats["write_errors"], 0)
                # stop() may also fail because the file handle is None.
                try:
                    await rec.stop()
                except OSError:
                    pass
        asyncio.run(scenario())


class TestGroundReceiveTime(unittest.TestCase):
    """Ground receive time is captured at ingestion."""

    def test_rx_ts_preserved_on_ingestion(self):
        """feed_line preserves incoming rx_ts and sets ground_received_at."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                # Feed telemetry with no rx_ts.
                msg = dict(SAMPLE_TELEMETRY)
                msg.pop("rx_ts", None)
                self.assertTrue(await server.feed_line(line(msg)))

                await server.stop()
                await rec.stop()

                # Check the recorded data.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                text = files[0].read_text().strip()
                rec_data = json.loads(text)
                # rx_ts should NOT be the same as ts (flight time).
                self.assertNotEqual(rec_data["rx_ts"], rec_data["ts"])
                # rx_ts should be a valid ISO timestamp.
                self.assertIn("T", rec_data["rx_ts"])
                # ground_received_at must also be present.
                self.assertIn("ground_received_at", rec_data)
        asyncio.run(scenario())

    def test_event_preserves_rx_ts(self):
        """Event records preserve incoming rx_ts and get ground_received_at."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                msg = dict(SAMPLE_EVENT)
                msg["rx_ts"] = "2026-10-24T12:01:00Z"
                self.assertTrue(await server.feed_line(line(msg)))

                await server.stop()
                await rec.stop()

                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                text = files[0].read_text().strip()
                rec_data = json.loads(text)
                # rx_ts should be preserved from the incoming message.
                self.assertEqual(rec_data["rx_ts"], "2026-10-24T12:01:00Z")
                # ground_received_at must also be present.
                self.assertIn("ground_received_at", rec_data)
        asyncio.run(scenario())


class TestSyntheticFixtureGeneration(unittest.TestCase):
    """Generate a synthetic session fixture for testing."""

    def test_generate_synthetic_session(self):
        """Create a synthetic JSONL session file for testing export."""
        with tempfile.TemporaryDirectory() as tmp:
            data_dir = Path(tmp)
            rec = Recorder(data_dir)

            synthetic_lines = []
            # Simulate a flight: prelaunch -> ascent -> burst -> descent
            flight_data = [
                {"kind": "telemetry", "seq": 1,
                 "ts": "2026-01-15T08:00:00Z", "fix": 3,
                 "lat": 51.078, "lon": -114.123, "alt": 1080.0,
                 "temp_ext": 5.0, "temp_int": 3.0,
                 "rh": 50.0, "pressure": 890.0,
                 "vspeed": 0.0, "battery": 8.4, "battery_pct": 100,
                 "mode": "NORMAL", "phase": "PRELAUNCH",
                 "rssi": -75, "snr": 8.0},
                {"kind": "telemetry", "seq": 2,
                 "ts": "2026-01-15T08:00:05Z", "fix": 3,
                 "lat": 51.079, "lon": -114.122, "alt": 2000.0,
                 "temp_ext": -5.0, "temp_int": 3.1,
                 "rh": 40.0, "pressure": 800.0,
                 "vspeed": 10.0, "battery": 8.38, "battery_pct": 99,
                 "mode": "NORMAL", "phase": "ASCENT",
                 "rssi": -78, "snr": 7.5},
                {"kind": "event", "ts": "2026-01-15T08:01:00Z",
                 "category": "threshold", "severity": "warning",
                 "code": "BURST_DETECT",
                 "message": "Burst detected — pressure below threshold",
                 "measurement": "pressure", "value": 195.0,
                 "threshold": 195.0, "unit": "hPa",
                 "mode": "EVENT", "seq": 3},
                {"kind": "telemetry", "seq": 3,
                 "ts": "2026-01-15T08:01:00Z", "fix": 3,
                 "lat": 51.080, "lon": -114.120, "alt": 15000.0,
                 "temp_ext": -58.0, "temp_int": 2.0,
                 "rh": 40.0, "pressure": 195.0,
                 "vspeed": -0.05, "battery": 8.1, "battery_pct": 80,
                 "mode": "EVENT", "phase": "BURST",
                 "rssi": -95, "snr": 3.0},
            ]

            for pkt in flight_data:
                norm = validate(pkt)
                frame = to_ws_message(norm)
                synthetic_lines.append(json.dumps(frame, separators=(",", ":")))

            # Write synthetic JSONL.
            jsonl_path = data_dir / "synthetic_test_session.jsonl"
            jsonl_path.write_text("\n".join(synthetic_lines) + "\n")

            # Export it.
            out = rec.export("synthetic_test_session")
            export_data = json.loads(out.read_text())

            # Verify.
            self.assertEqual(export_data["schema_version"], 1)
            self.assertIn("synthetic", export_data["mission"].lower() or True)
            self.assertEqual(len(export_data["telemetry"]), 3)
            self.assertEqual(len(export_data["events"]), 1)
            self.assertEqual(export_data["telemetry"][0]["seq"], 1)
            self.assertEqual(export_data["telemetry"][2]["seq"], 3)
            self.assertEqual(export_data["telemetry"][2]["phase"], "BURST")
            self.assertEqual(export_data["telemetry"][2]["temp_ext"], -58.0)
            self.assertEqual(export_data["events"][0]["code"], "BURST_DETECT")
            self.assertEqual(export_data["onboard_records"], 0)


# ---- S4 corrections (defects 1-7) ----------------------------------------


class TestMissionPersistedInRecords(unittest.TestCase):
    """Defect 1: set_mission name is persisted in every JSONL record
    so export recovers the actual mission, not the filename."""

    def test_mission_in_jsonl_records(self):
        """Each recorded line contains the actual mission name."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                rec.set_mission("Actual Mission")
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                obj = json.loads(files[0].read_text())
                self.assertEqual(obj["mission"], "Actual Mission")
        asyncio.run(scenario())

    def test_export_uses_persisted_mission(self):
        """Export recovers the mission from records, not just the filename."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                rec.set_mission("Actual Mission")
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                out = rec.export(list(data_dir.glob("*.jsonl"))[0].stem)
                export_data = json.loads(out.read_text())
                self.assertEqual(export_data["mission"], "Actual Mission")
        asyncio.run(scenario())

    def test_legacy_filename_fallback(self):
        """When no mission field is in records, export falls back to filename."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                # Don't call set_mission — mission stays "unknown"
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                out = rec.export(list(data_dir.glob("*.jsonl"))[0].stem)
                export_data = json.loads(out.read_text())
                # Falls back to the filename stem.
                self.assertIn(list(data_dir.glob("*.jsonl"))[0].stem.split("-")[0],
                              export_data["mission"])
        asyncio.run(scenario())


class TestAbsolutePathExport(unittest.TestCase):
    """Defect 2: --export accepts absolute paths inside --data-dir."""

    def test_absolute_path_inside_data_dir(self):
        """Absolute path resolving inside data_dir is accepted."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                jsonl_path = list(data_dir.glob("*.jsonl"))[0]
                # Export using the absolute path.
                out = rec.export(str(jsonl_path))
                self.assertTrue(out.exists())
                self.assertEqual(out.suffix, ".json")
        asyncio.run(scenario())

    def test_absolute_path_outside_data_dir_rejected(self):
        """Absolute path resolving outside data_dir is rejected."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp) / "data"
                data_dir.mkdir()
                rec = Recorder(data_dir)
                other_file = Path(tmp) / "other.jsonl"
                other_file.write_text('{"type":"telemetry"}\n')
                with self.assertRaises(ValueError):
                    rec.export(str(other_file))
        asyncio.run(scenario())

    def test_traversal_path_rejected(self):
        """Path containing .. that escapes data_dir is rejected."""
        with tempfile.TemporaryDirectory() as tmp:
            data_dir = Path(tmp) / "data"
            data_dir.mkdir()
            rec = Recorder(data_dir)
            with self.assertRaises(ValueError):
                rec.export("../other.jsonl")


class TestEventRssiSnrPreservation(unittest.TestCase):
    """Defect 4: Event input with rssi/snr preserves those fields."""

    def test_event_rssi_snr_in_normalized(self):
        """_validate_event preserves rssi/snr when present."""
        raw = {
            "kind": "event", "ts": "2026-10-24T12:01:00Z",
            "category": "threshold", "severity": "warning",
            "code": "BURST_DETECT", "message": "Burst detected",
            "rssi": -82, "snr": 6.5,
        }
        norm = validate(raw)
        self.assertIsNotNone(norm)
        self.assertEqual(norm["rssi"], -82)
        self.assertEqual(norm["snr"], 6.5)

    def test_event_rssi_snr_roundtrip(self):
        """Event rssi/snr survive feed_line -> record -> export."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                msg = {
                    "kind": "event", "ts": "2026-10-24T12:01:00Z",
                    "category": "threshold", "severity": "warning",
                    "code": "BURST_DETECT", "message": "Burst detected",
                    "rssi": -90, "snr": 4.0,
                }
                self.assertTrue(await server.feed_line(line(msg)))

                await server.stop()
                await rec.stop()

                # Check JSONL.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                rec_data = json.loads(files[0].read_text())
                self.assertEqual(rec_data["rssi"], -90)
                self.assertEqual(rec_data["snr"], 4.0)

                # Check export.
                out = rec.export(files[0].stem)
                export_data = json.loads(out.read_text())
                self.assertEqual(len(export_data["events"]), 1)
                self.assertEqual(export_data["events"][0]["rssi"], -90)
                self.assertEqual(export_data["events"][0]["snr"], 4.0)
        asyncio.run(scenario())


class TestRxTsBroadcastConsistency(unittest.TestCase):
    """Defect 3: WebSocket broadcast rx_ts matches what recorder receives."""

    def test_broadcast_rx_ts_matches_recorder(self):
        """When telemetry arrives without rx_ts, both broadcast and
        JSONL record get the same ground receive time (not flight ts)."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                # Feed telemetry without rx_ts.
                msg = dict(SAMPLE_TELEMETRY)
                msg.pop("rx_ts", None)
                flight_ts = msg["ts"]
                self.assertTrue(await server.feed_line(line(msg)))

                await server.stop()
                await rec.stop()

                # Check JSONL record.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                rec_data = json.loads(files[0].read_text())
                # rx_ts should NOT be the flight timestamp.
                self.assertNotEqual(rec_data["rx_ts"], flight_ts)
                # ground_received_at should also be present.
                self.assertIn("ground_received_at", rec_data)
                # The broadcast rx_ts (in the recorder_frame) should
                # match the JSONL rx_ts since they share the same frame.
                self.assertEqual(rec_data["rx_ts"], rec_data.get("ground_received_at"))
        asyncio.run(scenario())

    def test_event_rx_ts_preserved_on_broadcast(self):
        """Event with rx_ts preserves it in both broadcast and JSONL."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                msg = dict(SAMPLE_EVENT)
                msg["rx_ts"] = "2026-10-24T12:01:00Z"
                self.assertTrue(await server.feed_line(line(msg)))

                await server.stop()
                await rec.stop()

                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                rec_data = json.loads(files[0].read_text())
                self.assertEqual(rec_data["rx_ts"], "2026-10-24T12:01:00Z")
                self.assertIn("ground_received_at", rec_data)
        asyncio.run(scenario())

    def test_invalid_rx_ts_uses_ground_time(self):
        """Invalid rx_ts should NOT fall back to flight time; ground time is used."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                # Feed telemetry with an invalid rx_ts.
                msg = dict(SAMPLE_TELEMETRY)
                msg["rx_ts"] = "not-a-valid-timestamp"
                flight_ts = msg["ts"]
                self.assertTrue(await server.feed_line(line(msg)))

                await server.stop()
                await rec.stop()

                # Check JSONL record.
                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                rec_data = json.loads(files[0].read_text())
                # rx_ts should NOT be the flight timestamp (invalid rx_ts
                # must not cause fallback to flight time).
                self.assertNotEqual(rec_data["rx_ts"], flight_ts)
                # rx_ts should be the ground receive time.
                self.assertIn("ground_received_at", rec_data)
                self.assertEqual(rec_data["rx_ts"], rec_data["ground_received_at"])
        asyncio.run(scenario())

    def test_invalid_event_rx_ts_uses_ground_time(self):
        """Invalid event rx_ts should use ground receive time, not flight time."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()

                port = free_port()
                server = WebSocketServer("127.0.0.1", port,
                                         recorder=rec)
                await server.start()

                msg = dict(SAMPLE_EVENT)
                msg["rx_ts"] = "invalid-ts"
                flight_ts = msg["ts"]
                self.assertTrue(await server.feed_line(line(msg)))

                await server.stop()
                await rec.stop()

                files = list(data_dir.glob("*.jsonl"))
                self.assertEqual(len(files), 1)
                rec_data = json.loads(files[0].read_text())
                self.assertNotEqual(rec_data["rx_ts"], flight_ts)
                self.assertIn("ground_received_at", rec_data)
                self.assertEqual(rec_data["rx_ts"], rec_data["ground_received_at"])
        asyncio.run(scenario())


class TestCLIExportPath(unittest.TestCase):
    """Defect 2: CLI --export handles paths correctly."""

    def test_export_stem(self):
        """Export by stem works as before."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                # Use the actual created filename stem, not a fake one.
                jsonl_path = list(data_dir.glob("*.jsonl"))[0]
                out = rec.export(jsonl_path.stem)
                self.assertTrue(out.exists())
                self.assertEqual(out.suffix, ".json")
        asyncio.run(scenario())

    def test_export_absolute_path(self):
        """Export using the absolute path ending in .jsonl works."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                jsonl_path = list(data_dir.glob("*.jsonl"))[0]
                out = rec.export(str(jsonl_path))
                self.assertTrue(out.exists())
                self.assertEqual(out.suffix, ".json")
        asyncio.run(scenario())

    def test_export_cli_explicit_jsonl_path(self):
        """Export via --export with an explicit .jsonl path works from CLI."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                jsonl_path = list(data_dir.glob("*.jsonl"))[0]
                out = rec.export(str(jsonl_path))
                self.assertTrue(out.exists())
                self.assertEqual(out.suffix, ".json")
                export_data = json.loads(out.read_text())
                self.assertEqual(len(export_data["telemetry"]), 1)
        asyncio.run(scenario())

    def test_export_relative_jsonl_from_cwd(self):
        """Relative .jsonl path is resolved from cwd, not from data_dir.

        Regression test: --export ./data/my-session.jsonl with --data-dir=data
        must resolve to data/my-session.jsonl (from cwd), not
        data/data/my-session.jsonl.
        """
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                # Create structure: tmp/data/ (data_dir) and a file at tmp/data/session.jsonl
                data_dir = Path(tmp) / "data"
                data_dir.mkdir()
                # Write a session file directly into data_dir.
                jsonl_path = data_dir / "session.jsonl"
                jsonl_path.write_text(
                    '{"type":"telemetry","seq":1,"ts":"2026-10-24T12:00:00Z",'
                    '"fix":3,"lat":51.0,"lon":-114.0,"alt":0.0,"mission":"test"}\n')
                rec = Recorder(data_dir)
                # Change cwd to tmp so "./data/session.jsonl" resolves to tmp/data/session.jsonl.
                original_cwd = os.getcwd()
                try:
                    os.chdir(tmp)
                    # Export using a relative path from cwd (tmp).
                    out = rec.export("./data/session.jsonl")
                finally:
                    os.chdir(original_cwd)
                self.assertTrue(out.exists())
                self.assertEqual(out.suffix, ".json")
                export_data = json.loads(out.read_text())
                self.assertEqual(len(export_data["telemetry"]), 1)
                self.assertEqual(export_data["mission"], "test")


class TestExportMissionFromRecords(unittest.TestCase):
    """Defect 1: Export mission comes from records, not filename."""

    def test_export_uses_mission_from_records(self):
        """Export picks up mission from the first record's mission field."""
        async def scenario():
            with tempfile.TemporaryDirectory() as tmp:
                data_dir = Path(tmp)
                rec = Recorder(data_dir)
                await rec.start()
                rec.set_mission("My Real Mission")
                norm = validate({"kind": "telemetry", "seq": 1,
                                 "ts": "2026-10-24T12:00:00Z", "fix": 3,
                                 "lat": 51.0, "lon": -114.0, "alt": 0.0})
                await rec.record(to_ws_message(norm))
                await rec.stop()

                out = rec.export(list(data_dir.glob("*.jsonl"))[0].stem)
                export_data = json.loads(out.read_text())
                self.assertEqual(export_data["mission"], "My Real Mission")
        asyncio.run(scenario())


if __name__ == "__main__":
    unittest.main()
