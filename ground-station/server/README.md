# Balloon Bot — Ground Station Server

Python ground-station server for the ECE 490 capstone (Group 13). It ingests
telemetry from the ESP32-S3 ground receiver over USB serial, validates and
translates messages into the **existing dashboard's exact WebSocket contract**,
and broadcasts live data to connected dashboard clients. Local mission
recording is implemented in S4.

**Stage status:** **S3 — USB serial ingestion and recovery, implemented pending review**; **S4 — Ground recording + export, implemented pending review**.
Stages S5 is not yet implemented; see [implementation-plan.md](implementation-plan.md).

---

## 1. Architecture

```text
STM32 payload  --915 MHz LoRa-->  ESP32-S3 ground receiver
                                          |
                                     USB serial (newline-delimited JSON)
                                          |
                                   Python server (this directory)
                                          |
                                WebSocket (127.0.0.1:8765/ws)
                                          |
                                        Existing dashboard (interface/)
```

The STM32 remains responsible for sensing, onboard processing, event detection,
Normal/Event/Low-power modes, and microSD storage. **The server does not decide
or command flight modes.** In S3 it receives, validates, and forwards.

---

## 2. Setup

Requires Python **3.11+** (developed on 3.14).

```bash
cd ground-station/server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

`requirements.txt` currently lists lower bounds (`pyserial`, `websockets`); exact
versions will be pinned after the S2–S4 tests pass.

S3 verification uses a scripted serial producer; no physical ESP32-S3 is
required for this stage. The server also runs without hardware using fixtures.

---

## 3. CLI reference

```text
usage: ground-station-server [-h] [--host HOST] [--port PORT]
                             [--serial-port SERIAL_PORT] [--baud BAUD]
                             [--data-dir DATA_DIR]
                             [--log-level {DEBUG,INFO,WARNING,ERROR}]
                             [--fixture FILE] [--mission NAME]
                             [--export EXPORT]

Balloon Bot ground-station server (ESP32 serial -> dashboard WS).

options:
  -h, --help            show help and exit
  --host HOST           WebSocket bind host (default 127.0.0.1; loopback-only)
  --port PORT           WebSocket bind port (default 8765)
  --serial-port PORT    ESP32 USB serial device (e.g. /dev/cu.usbmodem1401);
                        omit to run without hardware
  --baud BAUD           serial baud rate (default 115200)
  --data-dir DIR        ground-records directory (default ./data); git-ignored
  --log-level LEVEL     log level (default INFO)
  --fixture FILE        JSONL fixture to replay through the ingestion path
                        (offline, deterministic; no hardware required)
  --mission NAME        mission name for the hello frame (default 'Balloon Bot')
  --export EXPORT       export a JSONL session to dashboard JSON shape.
                        Accepts a session filename stem (without .jsonl)
                        or a path ending in .jsonl (absolute paths are
                        allowed only when they resolve inside --data-dir).
                        Writes .json to the same directory.
```

**Default binding is loopback-only** (`127.0.0.1`). The server is never exposed
to the LAN by default.

Examples:

```bash
python3 main.py --help
python3 main.py                                   # no serial device (WS server only, S2+)
python3 main.py --serial-port /dev/cu.usbmodem1401 --baud 115200
python3 main.py --data-dir ./data --log-level DEBUG
python3 main.py --export my-session               # export a JSONL session to JSON
python3 main.py --export ./data/my-session.jsonl  # export with full path
```

---

## 4. Wire-format compatibility table

This is the S1 contract deliverable: **existing dashboard input** ↔ **Python
output** ↔ **ESP32 input**. The Python server emits the **exact** field names and
envelope that `interface/js/schema.js`, `link.js`, and `store.js` expect. No field
is renamed for stylistic consistency.

All timestamps are **ISO 8601 UTC strings**. Missing/unavailable measurements are
`null`, never omitted. One JSON object per WebSocket text frame.

| WS `type` | Dashboard input (schema.js) — exact fields | Python output | ESP32 serial input (`kind`) |
|---|---|---|---|
| `hello` | `schema_version`, `mission`, `launch_ts`, `launch{lat,lon}`, `firmware` | **synthesized by the server** from the first packet of a mission | — (server-synthesized; see §5) |
| `telemetry` | `seq`, `ts`, `rx_ts`, `pkt_type`, `valid`, `fix`, `lat`, `lon`, `alt`, `alt_baro`, `temp_ext`, `temp_int`, `rh`, `pressure`, `vspeed`, `battery`, `battery_pct`, `mode`, `phase`, `rssi`, `snr`, `ack` | forwarded/normalized from serial `telemetry` (fill `rx_ts`, normalize) | `telemetry` (same fields) |
| `event` | `id`, `ts`, `category`, `severity`, `code`, `message`, `measurement`, `value`, `threshold`, `unit`, `mode`, `seq` | forwarded/normalized from serial `event` | `event` (same fields) |
| `link` | `ts`, `radio` | mapped from serial `radio_status` | `radio_status` |
| `ack` | `ts`, `seq`, `ack` | mapped from serial `radio_ack` | `radio_ack` |

### Field types, units, and null/error behavior

| Field | Type | Unit | Notes |
|---|---|---|---|
| `seq` | integer ≥ 0 | — | packet sequence number; required for `telemetry`/`ack` |
| `ts` | ISO 8601 UTC | — | **payload measurement time** (STM32 clock); required |
| `rx_ts` | ISO 8601 UTC | — | **ground receive time** (ESP32/Python clock); defaults to `ts` |
| `pkt_type` | string | — | default `"telemetry"` |
| `valid` | boolean | — | `false` = corrupted frame (e.g. CRC fail); default `true` |
| `fix` | 0 \| 2 \| 3 | — | GNSS fix; `0` ⇒ `lat`/`lon`/`alt` are `null` |
| `lat`, `lon` | number | degrees | `null` when `fix == 0` |
| `alt` | number | m | GNSS altitude; `null` when `fix == 0` |
| `alt_baro` | number | m | barometric altitude; `null` if unavailable |
| `temp_ext` | number | °C | external/ambient; `null` if unavailable |
| `temp_int` | number | °C | internal/enclosure; `null` if unavailable |
| `rh` | number 0–100 | % | **internal/enclosure humidity** (SHT45); clamped 0–100 |
| `pressure` | number | hPa | MS5611 ambient pressure; `null` if unavailable |
| `vspeed` | number | m/s | vertical speed; `null` if unavailable |
| `battery` | number | V | pack voltage; `null` if unavailable |
| `battery_pct` | number 0–100 | % | estimated state of charge; `null` if unavailable |
| `mode` | `NORMAL`\|`EVENT`\|`LOW_POWER` | — | default `NORMAL` |
| `phase` | string \| null | — | `PRELAUNCH`\|`ASCENT`\|`CRUISE`\|`BURST`\|`DESCENT`\|`RECOVERED` |
| `rssi` | number | dBm | received signal strength; `null` if unavailable |
| `snr` | number | dB | signal-to-noise ratio; `null` if unavailable |
| `ack` | `pending`\|`acked`\|`missed` | — | telemetry defaults to `pending`; `radio_ack` defaults to `missed` (see §6) |

**Units:** the ESP32 sends measurements in the dashboard's native units (hPa,
°C, m, m/s, V, %, dBm, dB), so **no unit conversion is required**. If a future
firmware variant emits different units, every conversion must be documented here.

### `hello` synthesis

The dashboard expects a `hello` frame to initialize a connection. The server
**synthesizes** it (the ESP32 does not send one). Two rules:

1. **Per-client `hello` on connect.** Each newly connected dashboard client is
   sent a `hello` derived from the server's *current* known state — not only a
   broadcast `hello` when the first telemetry packet arrives. A browser that opens
   five minutes after launch still receives the correct initialization frame.
2. **No fabricated measurements.** When no mission data has yet arrived, the
   `hello` represents the state as unknown/disconnected using the existing
   contract (e.g. `launch: null`, radio `lost`), without inventing flight
   measurements.

The `hello` fields are: `mission` (from a stable mission id / config), `launch_ts`
(first known packet `ts` or a stable launch timestamp), `launch` (first packet
`lat`/`lon` if `fix > 0`, else `null`), `firmware` (from a `radio_status` report or
a configured string).

**New-mission detection:** a sequence-number reset alone is **not** conclusive
proof of a new mission — a payload reboot or a sequence-number wrap produces the
same symptom. The server uses a stable mission identifier or launch timestamp when
available. If neither exists, the limitation is documented (the server may
mis-detect a new mission) rather than silently assigning a definitively new
mission identity.

---

## 5. Serial message specification (ESP32 → Python)

**Framing:** one complete JSON object per line, terminated by `\n`. UTF-8.
Each line is an independent message. This is the proposed ESP32↔Python contract,
subject to S1 approval and to agreement with the ESP32 firmware workstream.

Four message kinds:

### 5.1 `telemetry`

```json
{
  "kind": "telemetry",
  "seq": 42,
  "ts": "2026-10-24T12:00:00.000Z",
  "rx_ts": "2026-10-24T12:00:00.123Z",
  "pkt_type": "telemetry",
  "valid": true,
  "fix": 3,
  "lat": 51.078, "lon": -114.123, "alt": 1234.5,
  "alt_baro": 1234.0,
  "temp_ext": -10.2, "temp_int": 3.1,
  "rh": 45.0,
  "pressure": 195.2,
  "vspeed": 5.2,
  "battery": 8.1, "battery_pct": 78,
  "mode": "NORMAL",
  "phase": "ASCENT",
  "rssi": -82, "snr": 6.5,
  "ack": "pending"
}
```

The ESP32 forwards the packet sequence, the payload measurement time (`ts`), and
the radio receive metadata (`rssi`, `snr`). `rx_ts` is the ground receive time
(set by the ESP32; the server may overwrite it with its own receive time).

### 5.2 `event`

```json
{
  "kind": "event",
  "id": "evt_burst_42",
  "ts": "2026-10-24T12:00:00.000Z",
  "category": "threshold",
  "severity": "warning",
  "code": "BURST_DETECT",
  "message": "Burst detected — pressure below threshold",
  "measurement": "pressure",
  "value": 195.2,
  "threshold": 195.0,
  "unit": "hPa",
  "mode": "EVENT",
  "seq": 42
}
```

Events are detected on the STM32 (sensing/onboard processing) and forwarded by the
ESP32. `category` ∈ `mode_change|threshold|gnss|battery|link|system`;
`severity` ∈ `info|warning|critical`.

### 5.3 `radio_status`

```json
{
  "kind": "radio_status",
  "ts": "2026-10-24T12:00:00.000Z",
  "radio": "connected",
  "rssi": -82,
  "snr": 6.5
}
```

Receiver state and useful receiver-side diagnostics. Maps to the dashboard `link`
message (`radio` ∈ `connected|degraded|lost`). `rssi`/`snr` here are receiver-side
diagnostics (not forwarded to the dashboard `link` frame, which only carries
`ts` + `radio`).

### 5.4 `radio_ack`

```json
{
  "kind": "radio_ack",
  "ts": "2026-10-24T12:00:00.000Z",
  "seq": 42,
  "ack": "acked"
}
```

An actual ACK-related event with a clearly defined direction (see §6). Maps to the
dashboard `ack` message.

**No application-level commands** are sent over serial. The serial link is
one-way (ESP32 → Python) for telemetry/events/status/ack; the server never commands
the payload.

---

## 6. Radio ACK definition

These are **different facts** and must not be collapsed:

1. **Ground receiver received flight telemetry** — the ESP32 successfully decoded
   the STM32's sequence-numbered packet over the air (confirmed uplink reception).
2. **Ground receiver transmitted a radio ACK** — the ESP32 sent an ACK frame back
   toward the payload (confirmed downlink transmission, provided the radio reports
   transmission completion).
3. **Flight payload actually received that radio ACK** — unknown unless separately
   confirmed by the payload or a defined protocol mechanism.
4. **Python received the ground receiver's serial report** — the server read the
   `radio_ack` line over USB.
5. **Browser received a WebSocket message** — the dashboard got the `ack` frame.

**Definition used by the server:** a `radio_ack` with `ack: "acked"` means
**fact 1 + fact 2** — the ground receiver confirmed successful over-the-air
reception of that sequence-numbered packet and transmitted an ACK. It does **not**
prove fact 3 (that the STM32 received the ACK).

**ACK default values — telemetry vs radio_ack.** These two serial message kinds
have *different* default `ack` values, and they must not be conflated:

- A `telemetry` line with no `ack` field defaults to **`"pending"`** — the
  packet was received but no ACK outcome is yet known. It is *not* a `missed`
  ACK; it is an unknown one, and it does not enter the ACK % denominator.
- A `radio_ack` line with no `ack` field defaults to **`"missed"`** — the
  ground receiver reports that the sequence-numbered packet was *not* received
  (fact 1 failed). This is the only value that enters the ACK % denominator
  alongside `acked`.

**Dashboard ACK % — exact numerator and denominator.** The dashboard computes
ACK % as:

```text
ACK % = (count of `ack` frames with ack == "acked")
        / (count of `ack` frames with ack == "acked"  or  ack == "missed")
        × 100
```

i.e. the fraction of ground-received packets for which the ESP32 confirmed
successful ground reception (`acked`) versus those it reported as not received
(`missed`). This is a **ground-side** metric. It is **not** a payload-confirmed
ACK success rate, and an ACK transmission rate must **not** be presented as proven
payload-side acknowledgement. If the hardware protocol cannot confirm fact 3,
that metric is reported as **unavailable**, not invented.

---

## 7. Bounded limits

| Limit | Value | Behavior when exceeded |
|---|---|---|
| Max serial line length | `MAX_LINE_BYTES = 4096` bytes | line dropped, counted as malformed, logged; server continues |
| Per-field validation | see §4 table | structurally invalid message → dropped, counted, logged; never forwarded as valid telemetry |
| Malformed line handling | drop + count + log | a bad line never crashes the server; the next valid line is processed normally |
| Read timeout | `READ_TIMEOUT_S = 1.0` s | no data for 1 s ⇒ link considered idle/lost; drives `radio_status` → `link` state |
| Reconnect behavior | `RECONNECT_MIN_S = 1.0` → `RECONNECT_MAX_S = 30.0` s | bounded exponential backoff on unplugged/missing/reset device; does not lock up the WebSocket server |
| Serial→normalizer queue | `QUEUE_MAX = 1000` lines | bounded queue; overflow is **reported** (logged + counted), not silently dropped |

---

## 8. Runbook

### Run the server (no hardware, S1)

```bash
cd ground-station/server
python3 main.py --help
python3 main.py
```

### Run the dashboard against the server (S2+)

1. Start the server: `python3 main.py` (binds `127.0.0.1:8765/ws`).
2. Serve the dashboard: `python3 -m http.server 8000` from `ground-station/interface/`.
3. Open the dashboard, go to **Settings**, set the WebSocket URL to
   `ws://localhost:8765/ws`, and click **Connect**.

### Ground records and export (S4 — implemented)

Ground recording is **active** in S4. Validated telemetry and event records are
written as append-only JSONL files into `ground-station/server/data/`
(git-ignored), one file per mission session.  Recording is independent of
WebSocket browser connections.

**Recording scope:** only `telemetry` and `event` records are written.
`radio_status` (link frames) and `radio_ack` (ack frames) are **not** recorded
as ground observations — they are link-state / control messages, not payload
measurements, and the dashboard's `parseSessionFile` does not expect them in
the historical export.

**Session lifecycle:**
* A new session file is created on the first validated record.
* Filename: `{mission}-{YYYYMMDD}-{HHMMSS}-{epoch_ns}.jsonl` — unique by
  nanosecond timestamp, safe for any filesystem.
* **Per-record flush:** each `record()` call writes one JSONL line + `\n` and
  calls `flush()` synchronously (off the event loop via `asyncio.to_thread`).
  The line reaches the OS write buffer before the method returns — no
  background buffering.  Full `fsync()` is called periodically (every 50
  records) and on `stop()`, so a crash between flushes may lose up to ~50
  records.
* **Stop:** `stop()` calls `fsync()` to force data through the OS page cache.
  On error the exception propagates (never silently swallowed).
* On restart a new session file is created; old files persist (recoverable).
* **Append mode:** session files are opened with `"a"` (not `"w"`) so they
  never truncate existing content.

**Ground receive time (`rx_ts`):** captured at ingestion time in `feed_line()`.
When the incoming raw message does not contain `rx_ts`, `rx_ts` is set to the
actual ground receive time (not the flight timestamp).  When `rx_ts` is
present in the incoming message it is preserved as-is.  Both `rx_ts` and
`ground_received_at` are stored in the JSONL record.

**Export:** `python3 main.py --export <session>` converts a JSONL session to
the dashboard's historical JSON shape:
`{ schema_version, mission, launch_ts, launch, telemetry: [], events: [],
  onboard_records }`.  Malformed complete lines are skipped with a **warning**;
the truncated last line is silently ignored.  Output is written atomically
(exclusive ``os.link()`` from a temp file) to avoid partial exports and never
overwrites an existing export.  If the output ``.json`` file already exists,
a ``-1.json`` suffix is used (then ``-2.json``, …) to avoid overwriting.

---

## 9. Stage status

| Stage | Status |
|---|---|
| S1 — Contracts + foundation | **approved** |
| S2 — WebSocket + fixtures | **approved** |
| S3 — Serial ingest + recovery | **implemented** (pending review) |
| S4 — Ground recording + export | **implemented** (pending review) |
| S5 — Physical integration | not started |

### S2 implementation notes

- `protocol.py` — full parse/validate/translate pipeline (mirrors `schema.js`).
- `websocket_server.py` — async WS server: loopback-only, per-client `hello` on
  connect (derived from current `MissionState`), non-blocking broadcast (a slow/
  disconnected client cannot stall ingestion), `feed_line` ingestion path, and
  deterministic per-client **fixture replay** (not a simulator).
- `fixture_source.py` — offline fixture input (JSONL lines) for deterministic
  integration tests.
- `main.py` — CLI with `--fixture` (replay a JSONL fixture to each new client).
- `tests/` — `test_protocol.py` (parse/validate/translate, happy + malformed) and
  `test_websocket.py` (connect/hello, multi-client fan-out, disconnect, invalid
  rejection, link/ack forwarding, reject-logging regression). 40 tests, all passing.
- `fixtures/sample.jsonl` — a sample 8-line fixture (telemetry/events/radio_status/
  radio_ack) for manual and automated testing.

**S2 verification:** 40/40 automated tests pass (test_protocol 28 + test_websocket 12); an end-to-end check confirmed a
connected client receives the hello (unknown state before data) plus all fixture
frames with correct mapped fields (altitude, temperature, pressure, GPS, mode,
phase, RSSI, SNR, ACK). A dashboard screenshot is captured for visual review.

**Next authorized action:** S3 (USB serial ingestion and recovery), after S2
approval.

### S3 implementation notes

- `serial_link.py` — `SerialLink`: dedicated reader thread (serial I/O never
  touches the asyncio event loop) + bounded `asyncio.Queue` (`QUEUE_MAX = 1000`
  lines; overflow is counted + logged, never silently dropped).
  - **Framing:** chunked reads (512 B, 0.1 s per-read timeout); bytes
    accumulate until `\n`; each complete line (newline stripped) is enqueued.
    A partial line (no newline yet) is buffered across reads (partial-line
    recovery); if the port dies mid-line the partial is discarded and state
    resets on the next successful open.
  - **Oversize:** a line longer than `MAX_LINE_BYTES` (4096) is dropped up to
    the next newline (counted + logged); if no newline appears within the
    limit the whole buffer is dropped. Each candidate line is length-checked
    individually before enqueueing, so a complete valid line that arrives in
    the same chunk after an oversize line is not stranded.
  - **Reconnect:** bounded exponential backoff (`RECONNECT_MIN_S = 1.0`
    doubling to `RECONNECT_MAX_S = 30.0`) on unplugged/missing/reset device;
    backoff resets on a successful open. Missing device at startup keeps the
    server operational (link state `reconnecting`), not a crash.
  - **States:** `disconnected` | `reconnecting` | `connected`
    (`SerialLink.state`); `last_line_time` (reset on each fresh open) drives
    the idle watchdog.
  - `serial_class` is injectable (defaults to `serial.Serial`) so tests use a
    scripted fake producer — no hardware required (S3 pass criterion).
- `main.py` — `ingest_loop` (serial lines → the S2 `feed_line` path) and
  `link_watchdog`.
  - **Link state exposure:** the dashboard contract only supports
    `radio` ∈ `connected|degraded|lost` on `link` frames. ESP32
    `radio_status` lines flow through `feed_line` unchanged; the watchdog
    only *forces* `lost` when the ground receiver is unreachable (serial not
    connected, or no line for `READ_TIMEOUT_S = 1.0` s) and broadcasts a
    `link` frame only on a state change (no spam).
  - **Shutdown:** `link.stop()` joins the reader thread, closes the port, and
    terminates the `lines()` iterator via a sentinel; the WS server is
    stopped afterwards.
- `tests/test_serial.py` — scripted serial producer (`FakeSerial` +
  `SerialFactory` emulating plug/unplug): valid-line e2e to a real WS
  client, malformed/truncated/invalid-UTF-8/oversize rejection, partial-line
  recovery, disconnect→reconnect resume without server restart, bounded
  backoff measurement, queue-overflow reporting, idle-watchdog `lost`
  broadcast + dedup, missing-at-startup, clean shutdown.

**S3 verification:** 52/52 automated tests pass
(`python3 -m unittest discover -s tests -q`; S3 adds 12 tests in
`test_serial.py` which require `pyserial` and are excluded when unavailable).
**Current Python suite (S4):** 106/106 tests pass (S2: 40 + S3 serial: 12 +
S4 recorder: 54).  Combined with 2 Node.js tests: 108 total checks.
(Serial tests require `pyserial`; excluded from discover when unavailable.)
The S3 pass criteria are met for
the scripted-producer evidence path:

- Well-formed serial messages reach the dashboard unchanged apart from
  documented normalization (test_valid_line_reaches_ws_client).
- Malformed JSON, truncated lines, invalid UTF-8, and oversized lines are
  handled without server crashes; the next valid line is processed normally
  (test_malformed_lines_no_crash_next_valid_ok, test_oversize_line_dropped_next_line_ok).
- USB disconnect/reconnect resumes live ingestion without restarting Python
  (test_disconnect_reconnect_resumes_ingest).
- The dashboard remains reachable and shows appropriate link-state/stale
  behavior during loss of serial input (test_idle_watchdog_broadcasts_lost,
  test_missing_at_startup_stays_operational).

**S3 evidence gap (documented, not claimed as passing):** the resumed-dashboard
screenshot `report_screenshots/s3-3-resumed.png` shows `RADIO: CONNECTED` but
**TELEMETRY STALE 00:54** — the serial-fed recovery path was exercised, but no
fresh packet arrived after the simulated reconnect in that capture. The
reconnect path itself is covered by `test_disconnect_reconnect_resumes_ingest`
(a scripted producer sends a fresh packet after reattachment and the client
receives it), but the screenshot alone does not prove new telemetry after
reconnect. No claim is made that this screenshot demonstrates fresh post-reconnect
telemetry; it documents the observed stale state.

### S4 implementation notes

- `recorder.py` — `Recorder`: append-only JSONL writer with **per-record flush**.
  Each accepted record is written synchronously (via `asyncio.to_thread`) so the
  line reaches the OS write buffer before `record()` returns. Full `fsync()`
  is called periodically (every 50 records) and on `stop()`. No background
  writer task or unbounded queue is used.
  - Session files are named `{mission}-{YYYYMMDD}-{HHMMSS}-{epoch_ns}.jsonl` in
    `data/` — unique by nanosecond timestamp.
  - Files are opened in **append mode** (`"a"`, not `"w"`) so they never truncate
    existing content.
  - Only `telemetry` and `event` messages are recorded (link/ack are control
    messages, not observations).
  - `start()` creates the data directory; the first `record()` opens a session
    file.
  - `stop()` flushes, calls `fsync()`, and closes the file. Errors propagate.
  - `record()` after `stop()` raises `RuntimeError` — it never silently opens
    a new session. An explicit subsequent ``start()`` or a fresh ``Recorder``
    instance is required to resume recording.
  - `export(session)` reads the JSONL file **line by line**, processes only
    newline-terminated complete records, skips malformed lines with a warning,
    and writes the dashboard-compatible JSON shape atomically using
    ``os.link()`` (exclusive create).  Never overwrites an existing export;
    appends ``-1.json``, ``-2.json``, … when the target already exists.
  - `ground_received_at` is preserved from the ingestion path; `rx_ts` is
    set from the raw message if present, otherwise to the ground receive time.
- `websocket_server.py` — `feed_line()` captures **ground receive time** at
  ingestion.  When the raw message lacks `rx_ts`, `rx_ts` is set to the
  actual ground receive time; when present it is preserved as-is.
  Recording happens **before** broadcast. Disk failures raise (never silently
  presented as success). The recorder is injectable via
  `WebSocketServer(recorder=...)`.
  - **Fixture replay is display-only:** replaying fixture lines to a new client
    does NOT call `feed_line()` and does NOT write to the recorder. This ensures
    multiple clients don't cause duplicate records.
  - **Ack frames:** `feed_line()` broadcasts ack frames but never records them
    (they are link-state / control messages, not observations).
- `main.py` — `--export <session>` CLI flag for standalone export. The recorder
  is started/stopped in the main lifecycle. The `--mission` config value is
  passed to the recorder via `set_mission()`. If WS startup or serial startup
  fails, the recorder is stopped and the exception propagates.
- `tests/test_recorder.py` — 54 tests covering: immediate write (line on disk
  before record() returns), append mode, unique filenames, malformed input
  rejection, partial final line recovery, export contract (exact shape, preserved
  timestamps/seq/RSSI/SNR, sorted by seq, no invented onboard_records), no-viewer
  recording, fixture replay (no duplication, no fake RF metadata), **two clients
  zero duplicate records**, `record()` after stop raises RuntimeError,
  `stop()` fsync error propagation, ground receive time capture,
  ack-not-recorded-through-feed-line, and event rx_ts preservation.
- `fixtures/` — synthetic test fixtures (clearly labelled, not real flight data):
  - `sample.jsonl` — 8-line S2 fixture (telemetry/events/radio_status/radio_ack)
  - `synthetic_flight.jsonl` — 5-line synthetic flight (prelaunch→ascent→burst→descent)
  - `synthetic_flight.json` — expected export shape for the synthetic flight

**S4 acceptance criteria:**
- [x] Recorded data exists after a normal restart and earlier complete
  records can be read following a simulated interrupted write.
- [x] Exported JSON loads in the dashboard without any frontend schema
  change (verified by Node.js dynamic-import test of `parseSessionFile`
  against both the fixture and a live Python recorder export).
- [x] UI verification of Data Logs import, session selection, date
  filters, and CSV export: **verified** via headless Chrome screenshot.
- [x] Sample synthetic session fixture: `fixtures/synthetic_flight.jsonl`
  and `fixtures/synthetic_flight.json` — clearly labelled synthetic.

**S4 test counts (S4-only, `tests/test_recorder.py`):** 54 tests pass.
Full Python suite (protocol 28 + websocket 12 + serial 12 + recorder 54): 106 tests pass.
Combined with 2 Node.js tests: 108 total checks.
(Serial tests require `pyserial`; excluded from discover when unavailable.)

**flush vs fsync:** `record()` calls `f.flush()` after each line so the OS
is notified immediately.  `stop()` calls `f.flush()` + `os.fsync()` to force
data through the OS page cache.  Additionally, every 50 records a periodic
`fsync()` is called (configurable via `fsync_interval`).  No background
buffering — the line reaches the OS write buffer before `record()` returns
(full fsync is periodic, not per-line).

**recording scope:** only `telemetry` and `event` records are written.
`radio_status` (link frames) and `radio_ack` (ack frames) are **not**
recorded as ground observations — they are link-state / control messages.

**ground_received_at:** captured at ingestion time in `feed_line()`.
When the incoming message does not contain `rx_ts`, `rx_ts` is set to the
actual ground receive time (not the flight timestamp).  When `rx_ts` is
present in the incoming message it is preserved as-is.  Both `rx_ts` and
`ground_received_at` are stored in the JSONL record.

**no silent writes:** if a write fails, the exception is logged and
re-raised.  A `record()` call that fails is **never** silently treated
as successful; the caller must decide whether to abort the server or
continue.

**filename:** `{mission}-{YYYYMMDD}-{HHMMSS}-{epoch_ns}.jsonl` — unique
by nanosecond timestamp, human-readable, safe for any filesystem.  Mission
name is sanitized (no `/`, `\`, `.`, `\x00`).  Files are opened with
`O_EXCL` to prevent collisions.

**export CLI path policy:** `python3 main.py --export <session>` accepts
either a session stem (looked up in `data_dir/`) or a path ending
in `.jsonl` (absolute paths are allowed only when they resolve inside
`data_dir`; paths escaping via ``..`` are rejected).  Output ``.json`` is
written next to the ``.jsonl`` file.  If the output already exists, a
numbered suffix is appended to avoid overwriting.
