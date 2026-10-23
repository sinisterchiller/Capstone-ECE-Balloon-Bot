# Decisions

- 2026-10-21: Dashboard is a **static vanilla-JS web app** — no build step, no npm, no CDN, no tile servers (must run offline in the field). Charts hand-rolled in SVG; map is a local lat/lon SVG projection (no map tiles).
- 2026-10-21: Telemetry reaches the dashboard over **WebSocket** (future Python server in `ground-station/server/`). A built-in **simulator mode** generates synthetic telemetry so the UI works without hardware.
- 2026-10-21: Historical sessions are loaded as JSON files via the UI (no server-side storage yet).
