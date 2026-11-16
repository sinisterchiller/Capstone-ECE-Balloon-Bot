"""Tests for protocol.py — parse / validate / translate (S2).

Covers the happy path and malformed/invalid inputs for every serial kind.
Run:  python3 -m unittest tests.test_protocol -v
"""

import json
import unittest

from protocol import (
    MAX_LINE_BYTES,
    encode_ws_message,
    parse_serial_line,
    to_ws_message,
    validate,
)


def line(obj) -> bytes:
    return (json.dumps(obj) + "\n").encode("utf-8")


class TestParseSerialLine(unittest.TestCase):
    def test_valid(self):
        raw = parse_serial_line(line({"kind": "telemetry", "seq": 1,
                                       "ts": "2026-10-24T00:00:00Z"}))
        self.assertIsNotNone(raw)
        self.assertEqual(raw["kind"], "telemetry")

    def test_oversized(self):
        self.assertIsNone(parse_serial_line(b"x" * (MAX_LINE_BYTES + 1)))

    def test_bad_utf8(self):
        self.assertIsNone(parse_serial_line(b"\xff\xfe\n"))

    def test_not_json(self):
        self.assertIsNone(parse_serial_line(b"hello\n"))

    def test_not_object(self):
        self.assertIsNone(parse_serial_line(b"[1,2,3]\n"))

    def test_empty(self):
        self.assertIsNone(parse_serial_line(b"\n"))

    def test_trailing_newline_stripped(self):
        raw = parse_serial_line(line({"kind": "radio_status", "radio": "connected"}))
        self.assertIsNotNone(raw)
        self.assertEqual(raw["radio"], "connected")

    def test_crlf_stripped(self):
        raw = parse_serial_line(b'{"kind":"radio_status","radio":"lost"}\r\n')
        self.assertIsNotNone(raw)


class TestValidate(unittest.TestCase):
    def test_telemetry_valid(self):
        raw = parse_serial_line(line({
            "kind": "telemetry", "seq": 42, "ts": "2026-10-24T00:00:00Z",
            "fix": 3, "lat": 51.0, "lon": -114.0, "alt": 100.0, "mode": "NORMAL",
        }))
        norm = validate(raw)
        self.assertIsNotNone(norm)
        self.assertEqual(norm["_ws_type"], "telemetry")
        self.assertEqual(norm["seq"], 42)
        self.assertEqual(norm["ack"], "pending")
        self.assertEqual(norm["rx_ts"], "2026-10-24T00:00:00Z")  # defaults to ts

    def test_telemetry_bad_seq(self):
        raw = parse_serial_line(line({"kind": "telemetry", "seq": -1,
                                       "ts": "2026-10-24T00:00:00Z"}))
        self.assertIsNone(validate(raw))

    def test_telemetry_noninteger_seq(self):
        raw = parse_serial_line(line({"kind": "telemetry", "seq": 1.5,
                                       "ts": "2026-10-24T00:00:00Z"}))
        self.assertIsNone(validate(raw))

    def test_telemetry_bad_ts(self):
        raw = parse_serial_line(line({"kind": "telemetry", "seq": 1, "ts": "not-a-date"}))
        self.assertIsNone(validate(raw))

    def test_telemetry_fix0_nulls_gps(self):
        raw = parse_serial_line(line({"kind": "telemetry", "seq": 1,
                                       "ts": "2026-10-24T00:00:00Z",
                                       "fix": 0, "lat": 51.0}))
        norm = validate(raw)
        self.assertEqual(norm["lat"], None)
        self.assertEqual(norm["lon"], None)
        self.assertEqual(norm["alt"], None)

    def test_telemetry_rh_clamped(self):
        raw = parse_serial_line(line({"kind": "telemetry", "seq": 1,
                                       "ts": "2026-10-24T00:00:00Z", "rh": 150.0}))
        norm = validate(raw)
        self.assertEqual(norm["rh"], 100.0)

    def test_telemetry_default_mode(self):
        raw = parse_serial_line(line({"kind": "telemetry", "seq": 1,
                                       "ts": "2026-10-24T00:00:00Z"}))
        norm = validate(raw)
        self.assertEqual(norm["mode"], "NORMAL")
        self.assertTrue(norm["valid"])

    def test_event_valid(self):
        raw = parse_serial_line(line({
            "kind": "event", "ts": "2026-10-24T00:00:00Z", "code": "BURST_DETECT",
            "category": "threshold", "severity": "warning",
        }))
        norm = validate(raw)
        self.assertEqual(norm["_ws_type"], "event")
        self.assertEqual(norm["category"], "threshold")

    def test_event_bad_ts(self):
        raw = parse_serial_line(line({"kind": "event", "ts": "bad"}))
        self.assertIsNone(validate(raw))

    def test_radio_status(self):
        raw = parse_serial_line(line({"kind": "radio_status", "radio": "connected"}))
        norm = validate(raw)
        self.assertEqual(norm["_ws_type"], "link")
        self.assertEqual(norm["radio"], "connected")

    def test_radio_status_default_lost(self):
        raw = parse_serial_line(line({"kind": "radio_status"}))
        norm = validate(raw)
        self.assertEqual(norm["radio"], "lost")

    def test_radio_ack(self):
        raw = parse_serial_line(line({"kind": "radio_ack", "seq": 5, "ack": "acked"}))
        norm = validate(raw)
        self.assertEqual(norm["_ws_type"], "ack")
        self.assertEqual(norm["seq"], 5)
        self.assertEqual(norm["ack"], "acked")

    def test_radio_ack_bad_seq(self):
        raw = parse_serial_line(line({"kind": "radio_ack", "seq": "x", "ack": "acked"}))
        self.assertIsNone(validate(raw))

    def test_radio_ack_default_missed(self):
        raw = parse_serial_line(line({"kind": "radio_ack", "seq": 1}))
        norm = validate(raw)
        self.assertEqual(norm["ack"], "missed")

    def test_unknown_kind(self):
        raw = parse_serial_line(line({"kind": "unknown"}))
        self.assertIsNone(validate(raw))


class TestToWsMessage(unittest.TestCase):
    def test_telemetry_frame(self):
        raw = parse_serial_line(line({
            "kind": "telemetry", "seq": 1, "ts": "2026-10-24T00:00:00Z",
            "fix": 3, "lat": 51.0, "lon": -114.0, "alt": 100.0,
        }))
        frame = to_ws_message(validate(raw))
        self.assertEqual(frame["type"], "telemetry")
        self.assertNotIn("_ws_type", frame)
        self.assertEqual(frame["seq"], 1)
        self.assertEqual(frame["ts"], "2026-10-24T00:00:00Z")

    def test_link_frame(self):
        raw = parse_serial_line(line({"kind": "radio_status", "radio": "degraded"}))
        frame = to_ws_message(validate(raw))
        self.assertEqual(frame["type"], "link")
        self.assertEqual(frame["radio"], "degraded")

    def test_ack_frame(self):
        raw = parse_serial_line(line({"kind": "radio_ack", "seq": 7, "ack": "acked"}))
        frame = to_ws_message(validate(raw))
        self.assertEqual(frame["type"], "ack")
        self.assertEqual(frame["seq"], 7)

    def test_encode_roundtrip(self):
        frame = {"type": "link", "radio": "connected"}
        self.assertEqual(json.loads(encode_ws_message(frame)), frame)

    def test_encode_returns_str(self):
        """Regression: encode_ws_message must return a *text* frame (str),
        not bytes. The websockets library sends a binary frame for bytes,
        which the dashboard's JSON.parse(e.data) cannot handle (arrives as a
        Blob). json.loads accepts both str and bytes, so the roundtrip test
        above does NOT catch this — this assertion does."""
        frame = {"type": "link", "radio": "connected"}
        encoded = encode_ws_message(frame)
        self.assertIsInstance(encoded, str)
        self.assertEqual(json.loads(encoded), frame)


if __name__ == "__main__":
    unittest.main()
