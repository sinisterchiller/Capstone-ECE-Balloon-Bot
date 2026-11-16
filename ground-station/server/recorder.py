"""recorder.py — append-only ground records and session export.

Writes validated observed telemetry/events as newline-delimited JSON records
(ground JSONL) into per-session files under a git-ignored data directory,
and exports a session to the dashboard's existing historical JSON importer
shape (``parseSessionFile`` in ``interface/js/schema.js``).

Recording does not depend on an active WebSocket browser connection.

Session lifecycle
-----------------
* **Start:** ``start()`` creates the data directory and enables recording.
  No session file is opened yet — that happens on the first valid ``record()``.
* **Filename:** ``{mission}-{YYYYMMDD}-{HHMMSS}-{epoch_ns}.jsonl`` — unique
  by nanosecond timestamp, human-readable, safe for any filesystem.
* **End:** on ``stop()`` the current file is flushed, fsync'd, and closed.
  A ``record()`` call after ``stop()`` raises ``RuntimeError`` — it never
  silently creates a new file.
* **Restart behaviour:** an explicit subsequent ``start()`` re-enables
  recording and allows a new session file to be created.  Old files persist
  on disk (recoverable).  A fresh ``Recorder`` instance also starts a new
  session, preserving old files.

Flush / close durability policy
-------------------------------
* ``record()`` writes one JSONL line + ``\\n`` and calls ``f.flush()`` so
  the OS is notified immediately.  Full ``fsync()`` is called periodically
  (every 50 records by default, configurable via ``fsync_interval``) and on
  ``stop()`` — a crash between flushes may lose up to ~50 records.
  ``record()`` validates the frame **before** opening a new session file,
  so invalid input never creates an empty file on disk.
* ``stop()`` calls ``f.flush()`` and ``os.fsync(f.fileno())`` to force the
  data through the OS page cache to disk.  If ``fsync`` fails the exception
  propagates (never silently swallowed).
* A crash between ``flush()`` and the next ``record()`` call may leave a
  truncated last line. The exporter handles this by skipping incomplete
  lines (see ``export()``, below).

Recording scope
---------------
Only ``telemetry`` and ``event`` records are written to the JSONL session
file.  ``radio_status`` (``link`` frames) and ``radio_ack`` (``ack`` frames)
are **not** recorded as ground observations — they are link-state /
control messages, not payload measurements, and the dashboard's
``parseSessionFile`` does not expect them in the historical export.

Disk-failure semantics
----------------------
If a write fails, the exception is logged and re-raised.  A ``record()``
call that fails is **never** silently treated as successful; the caller
must decide whether to abort the server or continue.
"""

from __future__ import annotations

import asyncio
import json
import logging
import os
import io
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

from protocol import to_ws_message, validate

log = logging.getLogger("recorder")

# How often to call os.fsync() during active recording (every N records).
# fsync on every line is expensive; this trades a small risk of losing the
# last ~fsync_interval records for dramatically better throughput.
FSYNC_INTERVAL = 50  # records between full fsync


class Recorder:
    """Append-only JSONL ground records + export to dashboard historical JSON.

    Each accepted record is written synchronously (off the event loop via
    ``asyncio.to_thread``) so the line reaches the OS write buffer before
    ``record()`` returns.  Full ``fsync()`` is called periodically (every 50
    records) and on ``stop()`` — a crash between flushes may lose up to ~50
    records.  No background writer task or unbounded queue is used.
    """

    def __init__(self, data_dir: Path, fsync_interval: int = FSYNC_INTERVAL) -> None:
        self.data_dir = data_dir
        self._fsync_interval = fsync_interval
        self._lock = asyncio.Lock()  # serialize record / stop
        # Session state.
        self._file: Optional[io.TextIOBase] = None  # file handle (text mode)
        self._session_path: Optional[Path] = None
        self._session_name: Optional[str] = None
        self._record_count: int = 0
        self._total_written: int = 0
        self._stopped = False
        self._started = False  # set True by start(), False by stop()
        # Stats.
        self.stats = {
            "records_written": 0,
            "bytes_written": 0,
            "fsync_count": 0,
            "write_errors": 0,
            "sessions_created": 0,
        }

    # -- Lifecycle --------------------------------------------------------

    async def start(self) -> None:
        """Create the data directory. Does NOT open a session file yet.

        The first ``record()`` call opens the session file.
        """
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self._stopped = False
        self._started = True
        log.info("Recorder started: data_dir=%s", self.data_dir)

    async def stop(self) -> None:
        """Flush, fsync, and close the current session file.

        If the file handle is already closed (no session was active) this
        is a no-op.  Errors during flush/fsync/close propagate.
        """
        async with self._lock:
            self._stopped = True
            self._started = False
            if self._file is not None:
                self._flush_sync()
                try:
                    os.fsync(self._file.fileno())
                    self.stats["fsync_count"] += 1
                except OSError as exc:
                    log.error("recorder: fsync on stop failed: %s", exc)
                    raise
                self._close_session()
            elif self._session_path is not None:
                # File handle is None but session path exists — can't fsync.
                raise OSError("session file handle is None, cannot fsync")
            log.info("Recorder stopped: wrote %d records across %d session(s)",
                     self.stats["records_written"], self.stats["sessions_created"])

    def set_mission(self, mission: str) -> None:
        """Set the mission name used in session metadata and records.

        Must be called before ``record()`` opens the first session file.
        The mission name is persisted in every recorded JSONL record
        so that export can recover it even without a filename match.
        """
        self._mission = mission

    def _mission_for_record(self) -> str:
        """Return the current mission name for inclusion in records."""
        return getattr(self, "_mission", "unknown")

    # -- Recording --------------------------------------------------------

    async def record(self, message: dict[str, Any]) -> None:
        """Append one validated message as a JSONL line.

        The record is written synchronously (off the event loop) so the
        complete JSONL line + newline reaches the OS write buffer before
        this method returns.  Full ``fsync()`` is called periodically
        (every 50 records) and on ``stop()``.

        Only ``telemetry`` and ``event`` messages are recorded (``link`` /
        ``ack`` frames are silently ignored — they are control/status, not
        observations).

        **Lifecycle enforcement:**

        * ``record()`` may only be called after ``start()`` has been
          invoked and before ``stop()`` has been called.
        * ``RuntimeError`` is raised if ``start()`` has not been called
          or if ``stop()`` has already been called.
        * The frame is validated **before** opening a new session file,
          so invalid input never creates an empty file on disk.
        * A fresh session can only be started by an explicit subsequent
          ``start()`` call or by creating a new ``Recorder`` instance.

        **Failure semantics:** if the write fails the exception is logged
        and re-raised.  The record is **not** silently lost.
        """
        async with self._lock:
            msg_type = message.get("type")
            if msg_type not in ("telemetry", "event"):
                return  # link/ack are control messages, not observations

            # Validate BEFORE opening a session to avoid empty files for
            # invalid input.
            norm_src = {**message, "kind": msg_type}
            normalized = validate(norm_src)
            if normalized is None:
                raise ValueError(
                    f"invalid {msg_type} frame after validate() returned None")

            if not self._started:
                raise RuntimeError(
                    "record() called without active session — "
                    "call start() first or do not call after stop()")

            if self._session_path is None:
                # First record in a fresh session (after explicit start()).
                self._open_session()

            # Serialize to JSONL — include the actual mission name
            # so export can recover it even if the filename is ambiguous.
            record = dict(message)
            record["mission"] = self._mission_for_record()
            line = json.dumps(record, separators=(",", ":"), ensure_ascii=False)

            # Write synchronously off the event loop.
            try:
                await asyncio.to_thread(self._write_line_sync, line)
            except (OSError, ValueError, AttributeError) as exc:
                self.stats["write_errors"] += 1
                raise

            self._total_written += 1
            self.stats["records_written"] += 1
            self.stats["bytes_written"] += len(line) + 1  # +1 for newline

            # Periodic fsync.
            self._record_count += 1
            if self._record_count % self._fsync_interval == 0:
                await asyncio.to_thread(self._fsync_sync)

    def _write_line_sync(self, line: str) -> None:
        """Synchronous write: write line + newline and flush."""
        if self._file is None:
            raise OSError("session file handle is None")
        self._file.write(line + "\n")
        self._file.flush()

    def _flush_sync(self) -> None:
        """Synchronous flush."""
        if self._file is not None:
            self._file.flush()

    def _fsync_sync(self) -> None:
        """Synchronous fsync."""
        if self._file is not None:
            os.fsync(self._file.fileno())
            self.stats["fsync_count"] += 1

    # -- Export -----------------------------------------------------------

    def export(self, session: str) -> Path:
        """Export a JSONL session to the dashboard historical JSON shape.

        **Parameters**
        * ``session`` — the session identifier (filename stem, or full path).
          If the path ends with ``.jsonl`` it is treated as a file path;
          otherwise ``{session}.jsonl`` is looked up in ``self.data_dir``.

        **Return value** — the path to the exported JSON file (``.json``).

        **Error handling**
        * Only valid stored telemetry/event envelopes are included.
        * Invalid complete lines are skipped with a **warning log**
          (contents never logged).
        * The last (possibly truncated) line without a trailing newline
          is silently ignored.
        * The output uses an atomic non-overwrite strategy: writes to a
          temporary file first, then publishes with ``os.link()`` (POSIX)
          or falls back to a ``-N.json`` suffix when the target already
          exists.  Never overwrites an existing export.

        **Exported shape** (matches ``parseSessionFile`` expectations):
        ``{ schema_version, mission, launch_ts, launch, telemetry: [],
           events: [], onboard_records }``
        """
        jsonl_path = self._resolve_session_path(session)
        if not jsonl_path.exists():
            raise FileNotFoundError(f"session file not found: {jsonl_path}")

        telemetry: list[dict] = []
        events: list[dict] = []
        mission: str = ""
        launch_ts: Optional[str] = None
        launch: Optional[dict] = None
        line_count = 0
        skip_count = 0

        # Read JSONL as bytes so we can enforce strict UTF-8 per line and
        # only process complete newline-terminated records.
        with open(jsonl_path, "rb") as fh:
            for raw_line in fh:
                # Only process lines that actually ended in a newline.
                if not raw_line.endswith(b"\n"):
                    continue
                stripped = raw_line.rstrip(b"\n\r")
                if not stripped:
                    continue
                line_count += 1
                # Strict UTF-8 decode — reject, never silently replace.
                try:
                    text = stripped.decode("utf-8")
                except UnicodeDecodeError:
                    skip_count += 1
                    log.warning("export: skipping non-UTF-8 line %d in %s",
                                line_count, jsonl_path.name)
                    continue
                try:
                    obj = json.loads(text)
                except json.JSONDecodeError:
                    skip_count += 1
                    log.warning("export: skipping malformed line %d in %s",
                                line_count, jsonl_path.name)
                    continue
                if not isinstance(obj, dict):
                    skip_count += 1
                    log.warning("export: skipping non-object line %d in %s",
                                line_count, jsonl_path.name)
                    continue

                msg_type = obj.get("type")
                if msg_type not in ("telemetry", "event"):
                    continue

                # Validate + coerce through the protocol pipeline so the
                # exported frame is dashboard-compatible.  Silently skip
                # records that fail validation.
                #
                # Stored JSONL records use "type" as the discriminator but
                # validate() dispatches on "kind" — build a transient copy
                # so validate() recognises the record without mutating the
                # original object.
                try:
                    norm_src = {"type": obj["type"], **obj, "kind": obj["type"]}
                    normalized = validate(norm_src)
                    if normalized is None:
                        skip_count += 1
                        log.warning("export: skipping invalid %s line %d in %s",
                                    msg_type, line_count, jsonl_path.name)
                        continue
                    frame = to_ws_message(normalized)
                    # validate() drops fields not in the protocol spec;
                    # preserve ground observation time for the dashboard.
                    if "ground_received_at" in obj:
                        frame["ground_received_at"] = obj["ground_received_at"]
                    if msg_type == "event" and "rx_ts" in obj:
                        frame["rx_ts"] = obj["rx_ts"]
                except Exception:
                    skip_count += 1
                    log.warning("export: skipping unparseable line %d in %s",
                                line_count, jsonl_path.name)
                    continue

                try:
                    if msg_type == "telemetry":
                        telemetry.append(frame)
                        if launch is None:
                            fix_val = frame.get("fix")
                            if isinstance(fix_val, int) and fix_val > 0:
                                launch = {
                                    "lat": frame.get("lat"),
                                    "lon": frame.get("lon"),
                                }
                        if launch_ts is None:
                            launch_ts = frame.get("ts")
                    elif msg_type == "event":
                        events.append(frame)

                    # Update mission name from the first record.
                    if mission == "" and "mission" in obj:
                        mission = obj["mission"]
                except Exception:
                    skip_count += 1
                    log.warning("export: skipping line %d in %s: unparseable record",
                                line_count, jsonl_path.name)
                    continue

        if skip_count:
            log.info("export: skipped %d malformed lines in %s",
                     skip_count, jsonl_path.name)

        # Sort telemetry by numeric sequence number.
        def _seq_key(t: dict) -> tuple:
            v = t.get("seq")
            if v is None:
                return (1, 0, "")
            if isinstance(v, (int, float)):
                return (0, v, "")
            return (0, 0, str(v))

        telemetry.sort(key=_seq_key)

        # Build the dashboard-compatible export.
        export_data = {
            "schema_version": 1,
            "mission": mission or jsonl_path.stem,
            "launch_ts": launch_ts,
            "launch": launch,
            "telemetry": telemetry,
            "events": events,
            "onboard_records": 0,  # ground records don't know onboard count
        }

        # --- Atomic write: never overwrite an existing export ----------
        out_path = jsonl_path.with_suffix(".json")

        def _write_atomic(target: Path) -> Path:
            """Write *export_data* atomically to *target*."""
            tmp = target.with_suffix(".json.tmp")
            fd = os.open(
                tmp,
                os.O_WRONLY | os.O_CREAT | os.O_EXCL,
                mode=0o644,
            )
            with os.fdopen(fd, "w", encoding="utf-8") as fh:
                json.dump(export_data, fh, indent=2, ensure_ascii=False)
                fh.write("\n")
            # POSIX exclusive publish: os.link() fails if target exists,
            # so the rename below is safe — it only creates a hard link
            # from an exclusive tmp, never overwriting.
            try:
                os.link(tmp, target)
                tmp.unlink(missing_ok=True)
            except FileExistsError:
                # Target already exists — unlink tmp and let caller pick
                # a new name.
                tmp.unlink(missing_ok=True)
                raise
            return target

        try:
            out_path = _write_atomic(out_path)
        except FileExistsError:
            # Target already exists — pick a -N.json suffix.
            stem = jsonl_path.stem
            counter = 1
            while True:
                candidate = jsonl_path.parent / f"{stem}-{counter}.json"
                try:
                    out_path = _write_atomic(candidate)
                    break
                except FileExistsError:
                    counter += 1

        log.info("exported session %s -> %s (%d telemetry, %d events)",
                 jsonl_path.name, out_path.name, len(telemetry), len(events))
        return out_path

    # -- Internal ---------------------------------------------------------

    def _open_session(self) -> None:
        """Create a new session file with exclusive create (O_EXCL).

        Uses ``O_EXCL`` so the open fails if the file already exists —
        eliminates the TOCTOU race of the old ``path.exists()`` check.
        The mission name is sanitized to prevent path-traversal.

        If the previous session file still exists on disk (e.g. after a
        ``stop()``), the ``O_EXCL`` open fails and we retry with a new
        nanosecond suffix until a unique name is found.
        """
        ts = datetime.now(timezone.utc)
        epoch_ns = int(time.time() * 1_000_000_000)
        mission = getattr(self, "_mission", "unknown")
        # Sanitize mission name: strip characters unsafe for filenames.
        safe_mission = "".join(
            c for c in mission if c not in ('/', '\\', '.', '\x00'))
        if not safe_mission:
            safe_mission = "unknown"
        for attempt in range(10):
            ns_suffix = epoch_ns + attempt
            filename = (
                f"{safe_mission}-{ts.strftime('%Y%m%d')}-"
                f"{ts.strftime('%H%M%S')}-{ns_suffix}.jsonl"
            )
            path = self.data_dir / filename
            try:
                fd = os.open(
                    path,
                    os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND,
                    mode=0o644,
                )
                self._session_path = path
                self._session_name = filename
                self._record_count = 0
                self._file = os.fdopen(fd, "a", encoding="utf-8")
                self.stats["sessions_created"] += 1
                log.info("recorder: opened session %s", path)
                return
            except FileExistsError:
                continue
        raise RuntimeError(
            f"could not create unique session file after 10 attempts "
            f"in {self.data_dir}"
        )


    def _close_session(self) -> None:
        """Close the current session file (no fsync — that's in stop()).

        Errors during flush/close propagate; they are no longer swallowed.
        """
        if self._file is not None:
            self._file.flush()
            self._file.close()
            self._file = None
        self._session_path = None
        self._session_name = None

    def _resolve_session_path(self, session: str) -> Path:
        """Resolve a session identifier to a file path within data_dir.

        Accepts either a stem (looked up in ``self.data_dir``) or an explicit
        ``.jsonl`` file path.  Absolute paths are allowed **only** when they
        resolve inside ``data_dir``; paths that escape via ``..`` or otherwise
        are rejected.  Relative ``.jsonl`` paths are resolved from the current
        working directory (not from ``data_dir``) so that ``--export
        ./data/session.jsonl`` works when ``--data-dir=data``.
        """
        p = Path(session)
        if p.suffix == ".jsonl":
            # Resolve the path from cwd (works for both absolute and
            # relative paths).  Only allow if it resolves inside data_dir.
            resolved = p.resolve()
            data_resolved = self.data_dir.resolve()
            if not str(resolved).startswith(str(data_resolved) + os.sep) \
                    and resolved != data_resolved:
                raise ValueError(
                    f"session path escapes data_dir: {session}")
            return resolved
        # Treat as stem — look up in data_dir.
        if ".." in session or "/" in session or "\\" in session:
            raise ValueError(
                f"session stem contains path traversal: {session}")
        resolved = (self.data_dir / f"{session}.jsonl").resolve()
        data_resolved = self.data_dir.resolve()
        if not str(resolved).startswith(str(data_resolved) + os.sep) \
                and resolved != data_resolved:
            raise ValueError(
                f"session stem resolves outside data_dir: {session}")
        return resolved
