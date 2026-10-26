"""websocket_server.py — WebSocket accept, broadcast, link-state (S2).

Accepts dashboard WebSocket clients on a loopback-only port and broadcasts live
frames. A per-client ``hello`` is sent on connect (derived from the server's
current mission state). Broadcast does not block on a slow/disconnected client,
so one bad browser cannot stall serial ingestion or recording.

The ``feed_line`` method is the single ingestion path: one newline-terminated
serial/fixture line -> parse -> validate -> translate -> broadcast. The same
path is used for serial input (S3) and fixture input (S2).
"""

from __future__ import annotations

import asyncio
import logging
import time
from datetime import datetime, timezone
from typing import Optional

from websockets.asyncio.server import serve

from protocol import (
    SCHEMA_VERSION,
    encode_ws_message,
    parse_serial_line,
    to_ws_message,
    validate,
)

log = logging.getLogger("websocket_server")

# Bounded per-client send timeout (seconds). A slow/stalled client's send is
# cancelled after this long and the client is dropped, so one bad client cannot
# block ingestion indefinitely.
SEND_TIMEOUT_S = 2.0

# Rejection log rate limit: first rejection in a quiet window is logged at INFO
# immediately; subsequent rejects within the window are counted silently. A
# summary is logged when a rejection arrives after the window elapses.
REJECT_LOG_WINDOW_S = 1.0


class MissionState:
    """Tracks the current mission so a per-client ``hello`` can be built.

    When no mission data has arrived, ``launch_ts``/``launch`` stay ``None`` so
    the hello represents the state as unknown/disconnected (no fabricated
    measurements).
    """

    def __init__(self, mission: str = ""):
        self.mission = mission
        self.launch_ts: Optional[str] = None
        self.launch: Optional[dict] = None
        self.firmware: str = ""
        self.last_packet: Optional[dict] = None
        self.last_radio: Optional[str] = None

    def update(self, frame: dict) -> None:
        t = frame.get("type")
        if t == "telemetry":
            self.last_packet = frame
            if frame.get("fix", 0) > 0 and self.launch is None:
                self.launch = {"lat": frame.get("lat"), "lon": frame.get("lon")}
            if self.launch_ts is None:
                self.launch_ts = frame.get("ts")
        elif t == "link":
            self.last_radio = frame.get("radio")

    def hello(self) -> dict:
        return {
            "type": "hello",
            "schema_version": SCHEMA_VERSION,
            "mission": self.mission,
            "launch_ts": self.launch_ts,
            "launch": self.launch,
            "firmware": self.firmware,
        }


class WebSocketServer:
    """Accepts dashboard WS clients and broadcasts live frames.

    Loopback-only by default (127.0.0.1). Per-client hello on connect.
    Broadcast is non-blocking with respect to slow/disconnected clients.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 8765,
                 state: Optional[MissionState] = None,
                 fixture_lines: Optional[list] = None,
                 send_timeout: float = SEND_TIMEOUT_S):
        self.host = host
        self.port = port
        self.state = state if state is not None else MissionState()
        self._clients: set = set()
        self._server = None
        self._fixture_lines = fixture_lines
        self.send_timeout = send_timeout
        self.stats = {"received": 0, "rejected": 0, "broadcast": 0,
                      "clients": 0, "dropped": 0}
        # Rejection-log rate-limiting state.
        self._reject_count = 0
        self._reject_last_log = 0.0
        self._reject_stage = ""

    @property
    def bound_port(self) -> Optional[int]:
        if self._server is None:
            return None
        return self._server.sockets[0].getsockname()[1]

    async def start(self) -> None:
        """Begin accepting connections on ``host:port``."""
        self._server = await serve(self._handler, self.host, self.port)
        log.info("WebSocket server listening on ws://%s:%d/ws", self.host, self.bound_port)

    async def _handler(self, websocket) -> None:
        self._clients.add(websocket)
        self.stats["clients"] = len(self._clients)
        # Per-client hello on connect, derived from the current state.
        await websocket.send(encode_ws_message(self.state.hello()))
        # S1 contract: deliver the contractually correct link state on
        # connection. The hello frame carries no radio field (dashboard
        # contract), so the current radio state goes in a `link` frame.
        if self.state.last_radio is not None:
            await websocket.send(encode_ws_message(
                {"type": "link",
                 "ts": datetime.now(timezone.utc).isoformat(),
                 "radio": self.state.last_radio}))
        # Replay the fixture to this client (deterministic replay, not a simulator).
        await self._replay_fixture(websocket)
        try:
            async for _ in websocket:
                # The dashboard sends no application messages in S2.
                pass
        finally:
            self._clients.discard(websocket)
            self.stats["clients"] = len(self._clients)

    async def _replay_fixture(self, websocket) -> None:
        """Replay the fixture lines to a single newly-connected client.

        This is a deterministic replay of recorded lines (not a telemetry
        simulator), so any client — including a browser opening the dashboard
        after startup — receives the fixture data.
        """
        if not self._fixture_lines:
            return
        for line in self._fixture_lines:
            raw = parse_serial_line(line)
            if raw is None:
                continue
            normalized = validate(raw)
            if normalized is None:
                continue
            frame = to_ws_message(normalized)
            self.state.update(frame)
            await websocket.send(encode_ws_message(frame))

    async def feed_line(self, line: bytes) -> bool:
        """Run one line through parse -> validate -> translate -> broadcast.

        Returns True if the line was accepted and broadcast, False if rejected
        (malformed / invalid). A rejected line is counted, never raised.

        ``link`` frames (from serial ``radio_status``) go through
        :meth:`set_link_state`, which updates the tracked radio state and
        broadcasts only on a state change (no duplicate spam).

        Rejections are logged at INFO with a bounded, useful message — the
        *kind* of rejection (parse vs validate) and the byte length, never the
        payload contents — so the default log level shows why lines are being
        dropped without flooding the console or leaking data.
        """
        raw = parse_serial_line(line)
        if raw is None:
            self.stats["rejected"] += 1
            self._log_reject("parse", line)
            return False
        normalized = validate(raw)
        if normalized is None:
            self.stats["rejected"] += 1
            self._log_reject("validate", line)
            return False
        frame = to_ws_message(normalized)
        if frame["type"] == "link":
            self.stats["received"] += 1
            await self.set_link_state(frame.get("radio"))
            return True
        self.state.update(frame)
        self.stats["received"] += 1
        await self.broadcast(encode_ws_message(frame))
        return True

    def _log_reject(self, stage: str, line: bytes) -> None:
        """Log one rejected line at INFO with modest rate limiting.

        Only the rejection stage and the line's byte length are logged — never
        the payload contents — so a flood of malformed lines stays readable and
        does not leak telemetry data into the console.

        The first rejection in a quiet window is logged immediately at INFO
        (visible at the default log level). Subsequent rejections within the
        window are counted silently — no per-line log flood. When a rejection
        arrives after ``REJECT_LOG_WINDOW_S`` seconds elapse, a summary of the
        accumulated count is logged before this line's first failure.
        """
        now = time.monotonic()
        if self._reject_count == 0:
            # First rejection since the last summary: log it immediately.
            log.info("rejected line (%s, %d bytes)", stage, len(line))
            self._reject_count = 1
            self._reject_last_log = now
            self._reject_stage = stage
        elif now - self._reject_last_log > REJECT_LOG_WINDOW_S:
            # Window elapsed: summarize the accumulated rejects, then log this one.
            log.info("rejected %d lines (%s) since last log",
                     self._reject_count, self._reject_stage)
            log.info("rejected line (%s, %d bytes)", stage, len(line))
            self._reject_count = 1
            self._reject_last_log = now
            self._reject_stage = stage
        else:
            # Within the window: count silently, no per-line log.
            self._reject_count += 1

    async def set_link_state(self, radio: Optional[str]) -> None:
        """Update the tracked radio state and broadcast a ``link`` frame on
        change.

        Central path for link-state changes: the serial ``radio_status``
        ingestion and the watchdog (forced ``lost`` on serial loss) both use
        it, so ``MissionState.last_radio`` stays authoritative and a newly
        connected client receives the current link state on connect (S1
        contract). No frame is sent when the state is unchanged (no spam).
        """
        if radio is None:
            return
        if radio != self.state.last_radio:
            self.state.last_radio = radio
            ts = datetime.now(timezone.utc).isoformat()
            await self.broadcast(encode_ws_message(
                {"type": "link", "ts": ts, "radio": radio}))

    async def broadcast(self, frame: str) -> None:
        """Send one frame to all connected clients without blocking on a slow client.

        ``frame`` is a UTF-8 string (a text WS frame). Each client's send is
        bounded by ``send_timeout``; a stalled client's send is cancelled after
        the timeout and the client is dropped, so one bad client cannot block
        ingestion indefinitely.
        """
        if not self._clients:
            return
        self.stats["broadcast"] += 1
        clients = list(self._clients)

        async def _send(c):
            try:
                await asyncio.wait_for(c.send(frame), timeout=self.send_timeout)
                return None
            except BaseException as e:  # timeout, stall, or disconnect
                return e

        results = await asyncio.gather(*(_send(c) for c in clients),
                                       return_exceptions=True)
        for c, r in zip(clients, results):
            if r is not None:
                # A failed/timed-out send means the client is gone; drop it.
                self._clients.discard(c)
                self.stats["clients"] = len(self._clients)
                self.stats["dropped"] += 1
                log.debug("dropped client on send failure/timeout: %s", r)

    async def stop(self) -> None:
        """Close all clients and stop accepting connections."""
        for c in list(self._clients):
            try:
                await c.close()
            except Exception:
                pass
        self._clients.clear()
        if self._server is not None:
            self._server.close()
            await self._server.wait_closed()
            self._server = None
