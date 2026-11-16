"""fixture_source.py — offline fixture input for deterministic integration tests (S2).

A small, controlled source of newline-terminated JSON lines. It mimics the
SerialLink.lines() interface so the server's ingestion path is identical whether
the input is serial (S3) or fixture (S2). This is NOT a telemetry simulator —
it just replays a fixed set of lines.
"""

from __future__ import annotations

from pathlib import Path
from typing import AsyncIterator, List


class FixtureSource:
    """Yields a fixed list of newline-terminated JSON lines (as bytes)."""

    def __init__(self, lines: List[bytes]):
        self._lines = lines

    @property
    def line_count(self) -> int:
        return len(self._lines)

    @classmethod
    def from_file(cls, path: Path) -> "FixtureSource":
        """Read a JSONL file (one JSON object per line) into a FixtureSource."""
        text = Path(path).read_text(encoding="utf-8")
        lines = [line.encode("utf-8") for line in text.splitlines() if line.strip()]
        return cls(lines)

    @classmethod
    def from_lines(cls, lines: List[bytes]) -> "FixtureSource":
        return cls(lines)

    async def lines(self) -> AsyncIterator[bytes]:
        for line in self._lines:
            yield line
