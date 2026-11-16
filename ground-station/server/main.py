"""main.py — CLI entry point for the ground-station server.

S4: WebSocket server + recorder + fixture input + USB serial ingestion
(SerialLink).  The recorder writes validated telemetry/events to append-only
JSONL files and supports ``--export`` to convert a session to the dashboard's
historical JSON shape.  Default binding is loopback-only (127.0.0.1) and
no physical hardware is required.

Run:
    python3 main.py --help
    python3 main.py                                   # WS server only (browsers connect)
    python3 main.py --fixture fixtures/sample.jsonl   # replay a fixture, then keep serving
    python3 main.py --serial-port /dev/cu.usbmodem1401 --baud 115200   # serial ingestion
    python3 main.py --export my-session               # export a session to JSON
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from fixture_source import FixtureSource
from protocol import READ_TIMEOUT_S
from recorder import Recorder
from serial_link import SerialLink
from websocket_server import MissionState, WebSocketServer

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 8765
DEFAULT_BAUD = 115200


@dataclass
class Config:
    """Runtime configuration (see ``parse_args``)."""

    host: str = DEFAULT_HOST
    port: int = DEFAULT_PORT
    serial_port: str | None = None
    baud: int = DEFAULT_BAUD
    data_dir: Path = Path("data")
    log_level: str = "INFO"
    fixture: str | None = None
    mission: str = "Balloon Bot"
    export: str | None = None


def parse_args(argv: list[str] | None = None) -> Config:
    """Parse CLI flags into a :class:`Config`.

    The WebSocket bind host defaults to loopback-only (127.0.0.1); it is never
    exposed to the LAN by default. The serial port is optional so the server
    runs without hardware.
    """
    parser = argparse.ArgumentParser(
        prog="ground-station-server",
        description="Balloon Bot ground-station server (ESP32 serial -> dashboard WS).",
    )
    parser.add_argument("--host", default=DEFAULT_HOST,
                        help=f"WebSocket bind host (default {DEFAULT_HOST}; loopback-only)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"WebSocket bind port (default {DEFAULT_PORT})")
    parser.add_argument("--serial-port", default=None,
                        help="ESP32 USB serial device (e.g. /dev/cu.usbmodem1401); "
                             "omit to run without hardware")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                        help=f"serial baud rate (default {DEFAULT_BAUD})")
    parser.add_argument("--data-dir", default="data",
                        help="ground-records directory (default ./data); git-ignored")
    parser.add_argument("--log-level", default="INFO",
                        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
                        help="log level (default INFO)")
    parser.add_argument("--fixture", default=None,
                        help="JSONL fixture file to replay through the ingestion path "
                             "(offline, deterministic; no hardware required)")
    parser.add_argument("--mission", default="Balloon Bot",
                        help="mission name for the hello frame (default 'Balloon Bot')")
    parser.add_argument("--export", default=None,
                        help="export a JSONL session to dashboard JSON shape. "
                             "Accepts a session filename stem (without .jsonl) "
                             "or a full path ending in .jsonl. Writes .json to "
                             "the same directory.")
    args = parser.parse_args(argv)
    return Config(
        host=args.host,
        port=args.port,
        serial_port=args.serial_port,
        baud=args.baud,
        data_dir=Path(args.data_dir),
        log_level=args.log_level,
        fixture=args.fixture,
        mission=args.mission,
        export=args.export,
    )


async def ingest_loop(server: WebSocketServer, link: SerialLink,
                      ingest_failed: asyncio.Event | None = None) -> None:
    """Consume serial lines into the S2 ingestion path (``feed_line``).

    Serial I/O is off the event loop inside ``SerialLink`` (dedicated reader
    thread + bounded queue); this task only moves complete lines through
    parse -> validate -> translate -> record -> broadcast.

    **Error handling:** if ``feed_line`` raises (e.g. disk error from the
    recorder), the exception is logged, the serial link is stopped (which
    signals the watchdog to exit), the ``ingest_failed`` event is set so the
    main loop can shut down, and the exception is re-raised so the caller's
    shutdown sequence runs and exits with non-zero status.
    The server never continues with a dead ingest task.

    When ``ingest_failed`` is omitted (e.g. in tests), a local ``asyncio.Event``
    is created so the exception handler still works; the production path
    (``run``) passes the explicit event shared with the main loop.
    """
    if ingest_failed is None:
        ingest_failed = asyncio.Event()
    try:
        async for line in link.lines():
            await server.feed_line(line)
    except Exception:
        log = logging.getLogger("main")
        log.exception("ingest_loop failed — shutting down")
        ingest_failed.set()
        await link.stop()
        raise


async def link_watchdog(server: WebSocketServer, link: SerialLink,
                        read_timeout: float = READ_TIMEOUT_S,
                        interval: float = 0.25) -> None:
    """Drive the dashboard link state from serial health (S3).

    The dashboard contract only supports ``radio`` in
    connected|degraded|lost on ``link`` frames. The ESP32's own
    ``radio_status`` lines flow through ``feed_line`` unchanged; this
    watchdog only *forces* ``lost`` when the ground receiver is
    unreachable:

    - serial not connected (unavailable / reconnecting) -> ``lost``
    - serial connected but no line for ``read_timeout`` -> ``lost``
      (S1 bounded limit: READ_TIMEOUT_S = 1.0 s)

    Goes through ``server.set_link_state`` (shared with the
    ``radio_status`` path): the state is tracked centrally and a ``link``
    frame is broadcast only on a state change (no spam); a newly connected
    client receives the current state on connect.
    """
    while True:
        if (link.state != "connected"
                or link.last_line_time is None
                or (time.monotonic() - link.last_line_time) > read_timeout):
            await server.set_link_state("lost")
        await asyncio.sleep(interval)


async def run(config: Config) -> None:
    """Orchestrate the server.

    S3/S4: starts the WebSocket server, recorder, and, when ``--serial-port``
    is given, the serial link (reader thread) plus the ingestion and
    link-watchdog tasks.  Fixture mode replays a fixture through the same
    ingestion path.  The server keeps running afterwards so browsers can
    connect and receive the per-client hello.

    When ``--export`` is given instead of ``--serial-port``, the server
    starts only long enough to export the named session and exits.
    """
    log = logging.getLogger("main")
    state = MissionState(mission=config.mission)

    # ---- Export-only mode (S4) ----------------------------------------
    if config.export:
        rec = Recorder(config.data_dir)
        await rec.start()
        try:
            out = rec.export(config.export)
            print(f"Exported session to {out}")
        finally:
            await rec.stop()
        return

    # ---- Normal server mode -------------------------------------------
    fixture_lines = None
    if config.fixture:
        fixture_lines = FixtureSource.from_file(Path(config.fixture))._lines
        log.info("Fixture mode: %s (%d lines) — replayed to each new client",
                 config.fixture, len(fixture_lines))

    recorder = Recorder(config.data_dir)
    await recorder.start()
    # Pass the actual mission name from Config into the recorder so session
    # metadata and filenames use the real mission, not "unknown".
    recorder.set_mission(config.mission)

    server = WebSocketServer(config.host, config.port, state, fixture_lines,
                             recorder=recorder)
    try:
        await server.start()
    except OSError as exc:
        log.error("WebSocket server failed to start: %s — shutting down", exc)
        await recorder.stop()
        raise

    link: Optional[SerialLink] = None
    tasks: list[asyncio.Task] = []
    ingest_task: Optional[asyncio.Task] = None
    ingest_exc: Optional[BaseException] = None

    # Event set when ingest_loop encounters a fatal error (e.g. disk failure).
    # Allows the main loop to exit its wait and run the shutdown sequence.
    ingest_failed = asyncio.Event()

    if config.serial_port:
        link = SerialLink(config.serial_port, config.baud)
        try:
            await link.start()
        except OSError as exc:
            log.error("Serial link failed to start: %s — shutting down", exc)
            await server.stop()
            await recorder.stop()
            raise
        ingest_task = asyncio.create_task(
            ingest_loop(server, link, ingest_failed), name="ingest")
        tasks.append(
            asyncio.create_task(link_watchdog(server, link), name="watchdog"))

    log.info("Server ready. Open the dashboard, set the WebSocket URL to "
             "ws://%s:%d/ws, and click Connect. Ctrl-C to stop.",
             config.host, server.bound_port)

    try:
        await asyncio.wait_for(
            ingest_failed.wait(),
            timeout=None,  # blocks until the event fires (ingest error)
        )
    except asyncio.TimeoutError:
        pass  # event never fired — normal run, will be cancelled on Ctrl-C
    except asyncio.CancelledError:
        # ingest_loop raised; exception captured when we gather below.
        pass
    finally:
        # Stop the serial link first: its sentinel terminates ingest_loop,
        # then cancel remaining tasks and stop the WS server.
        if link is not None:
            await link.stop()
        for t in tasks:
            t.cancel()
        if ingest_task is not None:
            try:
                await ingest_task
            except BaseException as exc:
                # Only record real errors; CancelledError from Ctrl-C
                # shutdown is normal and must not be re-raised.
                if not isinstance(exc, asyncio.CancelledError):
                    ingest_exc = exc
        await asyncio.gather(*tasks, return_exceptions=True)
        await server.stop()
        # Flush and close the recorder last so all remaining records are
        # written to disk before the server shuts down. Errors propagate.
        await recorder.stop()
    if ingest_exc is not None:
        raise ingest_exc from None
    # Surface watchdog unexpected failure if present.
    for t in tasks:
        if t.done() and not t.cancelled():
            exc = t.exception()
            if exc is not None:
                raise exc from None


def main(argv: list[str] | None = None) -> int:
    """CLI entry point."""
    config = parse_args(argv)
    logging.basicConfig(level=config.log_level,
                        format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    try:
        asyncio.run(run(config))
    except Exception as exc:
        # Catch only real errors (not KeyboardInterrupt / CancelledError
        # from Ctrl-C) so the process exits with non-zero status and a
        # log line.  KeyboardInterrupt propagates naturally with exit 1.
        logging.error("Server exited with error: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
