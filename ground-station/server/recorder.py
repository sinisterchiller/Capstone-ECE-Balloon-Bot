"""recorder.py — append-only ground records and session export. S1 stub (logic in S4).

Writes validated observed telemetry/events as append-only newline-delimited JSON
records (one per line) into per-session files under a git-ignored data directory,
and exports a session to the dashboard's existing historical JSON importer shape.
Recording must not depend on an active browser connection.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any


class Recorder:
    """Append-only JSONL ground records + export to dashboard historical JSON.

    S1 stub: interface only. Recording, flush/close, and export logic land in S4.
    """

    def __init__(self, data_dir: Path):
        self.data_dir = data_dir

    async def start(self) -> None:
        """Create the data directory and open the session file. S4 — not implemented in S1."""
        raise NotImplementedError("S4")

    async def stop(self) -> None:
        """Flush and close the session file. S4 — not implemented in S1."""
        raise NotImplementedError("S4")

    async def record(self, message: dict[str, Any]) -> None:
        """Append one validated message as a JSONL line. S4 — not implemented in S1."""
        raise NotImplementedError("S4")

    def export(self, session: str) -> Path:
        """Export a session to the dashboard historical JSON shape. S4 — not implemented in S1."""
        raise NotImplementedError("S4")
