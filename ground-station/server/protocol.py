"""protocol.py — single source of truth for the ground-station wire contract.

Implements (S2) the parse / validate / translate pipeline that turns one
newline-terminated serial line (ESP32 -> Python) into the exact WebSocket frame
the existing dashboard expects (mirrors interface/js/schema.js).

The constants mirror ``interface/js/schema.js`` so the Python server and the
dashboard stay in sync. See README.md for the full compatibility table and
serial specification.
"""

from __future__ import annotations

import json
from datetime import datetime
from typing import Any, Optional

# ---- Enums (mirror interface/js/schema.js) ------------------------------
SCHEMA_VERSION = 1
MODES = ("NORMAL", "EVENT", "LOW_POWER")
PHASES = ("PRELAUNCH", "ASCENT", "CRUISE", "BURST", "DESCENT", "RECOVERED")
SEVERITIES = ("info", "warning", "critical")
CATEGORIES = ("mode_change", "threshold", "gnss", "battery", "link", "system")
FIX_VALUES = (0, 2, 3)
ACK_STATES = ("pending", "acked", "missed")
RADIO_STATES = ("connected", "degraded", "lost")

# Serial message kinds (ESP32 -> Python), one JSON object per newline-terminated line.
SERIAL_KINDS = ("telemetry", "event", "radio_status", "radio_ack")

# WebSocket message types (Python -> dashboard); mirror schema.js `type` values.
WS_TYPES = ("hello", "telemetry", "event", "link", "ack")

# ---- Bounded limits (S1 decisions) --------------------------------------
MAX_LINE_BYTES = 4096
READ_TIMEOUT_S = 1.0
RECONNECT_MIN_S = 1.0
RECONNECT_MAX_S = 30.0
QUEUE_MAX = 1000


# ---- Coercion helpers (mirror schema.js num / str / oneOf / parseTs) -----

def _num(value: Any) -> Optional[float]:
    """Coerce to a finite float, or None."""
    if value is None or isinstance(value, bool):
        return None
    try:
        n = float(value)
    except (TypeError, ValueError):
        return None
    return n if n == n else None  # reject NaN


def _int(value: Any) -> Optional[int]:
    """Coerce to an integer (rejects bools and non-integer floats), or None."""
    if value is None or isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return None


def _str(value: Any, fallback: Optional[str] = None) -> Optional[str]:
    return value if isinstance(value, str) else fallback


def _one_of(value: Any, allowed, fallback: Any) -> Any:
    return value if value in allowed else fallback


def _bool(value: Any, fallback: bool = True) -> bool:
    return value if isinstance(value, bool) else fallback


def _parse_iso_ms(value: Any) -> Optional[int]:
    """Parse an ISO 8601 string to epoch milliseconds, or None if invalid."""
    if not isinstance(value, str):
        return None
    s = value.strip()
    if not s:
        return None
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    try:
        dt = datetime.fromisoformat(s)
    except ValueError:
        return None
    return int(dt.timestamp() * 1000)


def _clamp(value: Optional[float], lo: float, hi: float) -> Optional[float]:
    return None if value is None else min(hi, max(lo, value))


# ---- Parse ----------------------------------------------------------------

def parse_serial_line(line: bytes) -> Optional[dict]:
    """Parse one newline-terminated serial line into a raw dict.

    Enforces ``MAX_LINE_BYTES``, UTF-8 decoding, and JSON object shape.
    Returns ``None`` for oversized, non-UTF-8, non-JSON, or non-object lines
    (malformed lines are dropped, never raised).
    """
    if len(line) > MAX_LINE_BYTES:
        return None
    try:
        text = line.decode("utf-8")
    except UnicodeDecodeError:
        return None
    text = text.rstrip("\r\n")
    if not text:
        return None
    try:
        raw = json.loads(text)
    except json.JSONDecodeError:
        return None
    if not isinstance(raw, dict):
        return None
    return raw


# ---- Validate -------------------------------------------------------------

def validate(raw: dict) -> Optional[dict]:
    """Validate + coerce one serial message against the contract.

    Returns a normalized dict (with an internal ``_ws_type`` key) or ``None``
    if structurally invalid.
    """
    kind = raw.get("kind")
    if kind == "telemetry":
        return _validate_telemetry(raw)
    if kind == "event":
        return _validate_event(raw)
    if kind == "radio_status":
        return _validate_radio_status(raw)
    if kind == "radio_ack":
        return _validate_radio_ack(raw)
    return None


def _validate_telemetry(raw: dict) -> Optional[dict]:
    seq = _int(raw.get("seq"))
    if seq is None or seq < 0:
        return None
    ts = raw.get("ts")
    if _parse_iso_ms(ts) is None:
        return None
    rx_ts = raw.get("rx_ts", ts)
    if _parse_iso_ms(rx_ts) is None:
        rx_ts = ts
    fix = _one_of(raw.get("fix"), FIX_VALUES, 0)
    has_fix = fix > 0
    return {
        "_ws_type": "telemetry",
        "seq": seq,
        "ts": ts,
        "rx_ts": rx_ts,
        "pkt_type": _str(raw.get("pkt_type"), "telemetry"),
        "valid": _bool(raw.get("valid"), True),
        "fix": fix,
        "lat": _num(raw.get("lat")) if has_fix else None,
        "lon": _num(raw.get("lon")) if has_fix else None,
        "alt": _num(raw.get("alt")) if has_fix else None,
        "alt_baro": _num(raw.get("alt_baro")),
        "temp_ext": _num(raw.get("temp_ext")),
        "temp_int": _num(raw.get("temp_int")),
        "rh": _clamp(_num(raw.get("rh")), 0, 100),
        "pressure": _num(raw.get("pressure")),
        "vspeed": _num(raw.get("vspeed")),
        "battery": _num(raw.get("battery")),
        "battery_pct": _clamp(_num(raw.get("battery_pct")), 0, 100),
        "mode": _one_of(raw.get("mode"), MODES, "NORMAL"),
        "phase": _str(raw.get("phase")),
        "rssi": _num(raw.get("rssi")),
        "snr": _num(raw.get("snr")),
        "ack": _one_of(raw.get("ack"), ACK_STATES, "pending"),
    }


def _validate_event(raw: dict) -> Optional[dict]:
    ts = raw.get("ts")
    if _parse_iso_ms(ts) is None:
        return None
    seq = _int(raw.get("seq"))
    return {
        "_ws_type": "event",
        "id": _str(raw.get("id"), f"evt_{ts}"),
        "ts": ts,
        "category": _one_of(raw.get("category"), CATEGORIES, "system"),
        "severity": _one_of(raw.get("severity"), SEVERITIES, "info"),
        "code": _str(raw.get("code"), ""),
        "message": _str(raw.get("message"), ""),
        "measurement": _str(raw.get("measurement")),
        "value": _num(raw.get("value")),
        "threshold": _num(raw.get("threshold")),
        "unit": _str(raw.get("unit")),
        "mode": _one_of(raw.get("mode"), MODES, None),
        "seq": seq,
        # RF metadata may be present on events that arrive with the
        # same radio reception record as the enclosing telemetry.
        "rssi": _num(raw.get("rssi")),
        "snr": _num(raw.get("snr")),
    }


def _validate_radio_status(raw: dict) -> Optional[dict]:
    ts = raw.get("ts")
    if _parse_iso_ms(ts) is None:
        ts = None
    return {
        "_ws_type": "link",
        "ts": ts,
        "radio": _one_of(raw.get("radio"), RADIO_STATES, "lost"),
    }


def _validate_radio_ack(raw: dict) -> Optional[dict]:
    seq = _int(raw.get("seq"))
    if seq is None or seq < 0:
        return None
    ts = raw.get("ts")
    if _parse_iso_ms(ts) is None:
        ts = None
    return {
        "_ws_type": "ack",
        "ts": ts,
        "seq": seq,
        "ack": _one_of(raw.get("ack"), ("acked", "missed"), "missed"),
    }


# ---- Translate ------------------------------------------------------------

def to_ws_message(normalized: dict) -> dict:
    """Translate a normalized dict into a dashboard WS frame.

    Adds the ``type`` discriminator and drops internal ``_``-prefixed keys.
    """
    frame = {k: v for k, v in normalized.items() if not k.startswith("_")}
    frame["type"] = normalized["_ws_type"]
    return frame


def encode_ws_message(message: dict) -> str:
    """Serialize a WS frame to a UTF-8 string (one JSON object per text frame).

    Returns a ``str`` (not ``bytes``) so the ``websockets`` library sends a
    *text* frame — the dashboard's ``JSON.parse(e.data)`` expects a text
    message, not a binary Blob.
    """
    return json.dumps(message, separators=(",", ":"), ensure_ascii=False)
