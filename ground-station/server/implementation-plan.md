# ECE 490 Capstone — Ground Station Python Server Implementation Plan

**Project:** Balloon Bot — Group 13  
**Target branch:** `feature/ground-station`  
**Repository:** `/Users/anuragkoushik/Desktop/Proj/Capstone-ECE-Balloon-Bot`  
**Implementation directory:** `ground-station/server/`  
**Status:** **S1–S4 complete** — all non-hardware server work (contracts,
WebSocket delivery, serial ingestion, ground recording + export) is
implemented and the full automated suite passes (108 Python tests). **S5 —
physical integration** is the only remaining stage and is blocked on real
ESP32-S3/STM32 hardware (no hardware validation is claimed).
**Scope:** Python ground-station server only; ESP32 and STM32 firmware are separate workstreams.

---

## 1. Objective and boundaries

Build a small Python service that ingests telemetry from the ESP32-S3 ground receiver over USB serial, validates and translates messages into the **existing dashboard's exact WebSocket contract**, records mission sessions locally, and broadcasts live data to connected dashboard clients.

Intended physical pipeline:

```text
STM32 payload  --915 MHz LoRa-->  ESP32-S3 ground receiver
                                          |
                                     USB serial
                                          |
                                   Python server
                                     |       |
                                  JSONL    WebSocket
                                  files       |
                                        Existing dashboard
```

**The STM32 remains responsible for sensing, onboard processing, event detection, Normal/Event/Low-power modes, and microSD storage.** The server does **not** decide or command flight modes.

### In scope

- Local USB serial ingestion from an ESP32-S3, with status and reconnection handling.
- Decoding, validation, and translation of ground receiver messages.
- Live WebSocket broadcast to the existing vanilla-JS dashboard.
- Correct treatment of packet sequence numbers, received signal metadata, and radio acknowledgements.
- Ground-side mission recording and export to the dashboard's **existing** historical JSON import format.
- Small automated tests and observable manual integration tests.

### Out of scope

- STM32/ESP32 firmware implementation, LoRa PHY/radio selection, and antenna hardware design.
- Browser control of payload operating modes, remote firmware flashing, or command uplinks.
- A new dashboard, framework migration, REST API, MQTT broker, cloud infrastructure, SQL database, authentication system, or mobile application.
- Hardware-performance claims before real measurements exist.

### Existing project constraints

- Dashboard phase is complete and committed as `55e3a65`; do **not** reopen UI development for optional enhancements.
- Existing dashboard uses vanilla JavaScript ES modules, offline assets, and a WebSocket client.
- Historical sessions are loaded as JSON files through the dashboard.
- Simulator ACK percentages and event data are synthetic, **not hardware validation**.
- Maintain the step-gated workflow: implement one stage, test, document evidence, request review, then proceed.

---

## 2. Proposed design

### Runtime and dependencies

- Python **3.11+**.
- `pyserial` for serial reads.
- `websockets` for WebSocket clients (`websockets.asyncio` API where supported by the pinned version).
- Python standard library for `asyncio`, `argparse`, `threading`/`asyncio.to_thread`, `json`, `logging`, `datetime`, `pathlib`, and `unittest`.
- Dependencies are pinned in `requirements.txt` to the versions verified in
  `ground-station/.venv`: `pyserial==3.5`, `websockets==17.3` (Python 3.11+).
  No unnecessary packages are introduced.

### Initial operating defaults (provisional, not a verified hardware contract)

| Setting | Initial choice | Notes |
|---|---|---|
| WebSocket host | `127.0.0.1` | Local laptop only; do not expose to LAN by default |
| WebSocket port | `8765` | Must agree with the dashboard Settings value |
| Serial baud | `115200` | Confirm with ESP32 firmware later |
| Serial device | CLI-configured | e.g. `/dev/cu.usbmodem...` on macOS; no hard-coded port |
| Ground records | `ground-station/server/data/` | Git-ignored |
| Serial framing | Newline-delimited UTF-8 JSON | Proposed ESP32↔Python contract, subject to S1 approval |
| Dashboard messages | **Existing contract only** | Determine by reading repository source in S1 |

### Suggested repository structure

```text
ground-station/server/
├── main.py                  # CLI, startup, shutdown, task orchestration
├── serial_link.py           # ESP32 serial receive and reconnect
├── protocol.py              # Parse/validate/normalize/translate
├── websocket_server.py      # WebSocket accept, broadcast, link-state
├── recorder.py              # Append-only ground records, export
├── requirements.txt
├── README.md                # Runbook, CLI, message contracts
├── tests/
│   ├── test_protocol.py
│   ├── test_recorder.py
│   └── test_integration.py
└── data/                    # generated; git-ignored
```

Only introduce additional modules when a concrete requirement makes them necessary. Never scatter copies of the telemetry schema through multiple modules.

### Server event flow

```text
Serial line / controlled test fixture
              |
        parse and validate
              |
    normalize timestamp, ID,
    sequence, radio metadata
              |
       record valid event
              |
   translate to dashboard format
              |
        fan out to clients
```

The serial reader must not block the asyncio WebSocket event loop. Reading from the serial device may use a dedicated thread or `asyncio.to_thread`; pass complete lines into a bounded queue and process them in one normalizer/recording path. Specify how queue overflow is reported rather than silently losing data. Keep the implementation simple.

---

## 3. Contracts to settle before coding (S1)

**Do not invent the live WebSocket JSON schema.** First inspect the current versions of at least:

- `ground-station/interface/js/schema.js`
- `ground-station/interface/js/link.js`
- `ground-station/interface/js/store.js`
- `ground-station/interface/js/simulator.js`
- `ground-station/interface/js/main.js`
- Data Logs import/export implementation under `ground-station/interface/js/pages/`
- Any available dashboard test fixtures or examples.

Record the actual field names, units, packet/event message types, timestamp semantics, WebSocket lifecycle behavior, ACK fields, and historical session JSON structure. Reuse them exactly where possible. Do not rename fields merely for stylistic consistency.

### A. ESP32-S3 → Python serial format (proposal)

Use **one complete JSON object per line**, terminated by `\n`. Agree on the contract with the ESP32 firmware workstream. Support a minimal, versioned set of message kinds:

| Serial message kind | Purpose |
|---|---|
| `telemetry` | Decoded flight packet, sequence number, received RF metadata, measured fields |
| `radio_status` | Receiver state and useful receiver-side diagnostics |
| `radio_ack` | Actual ACK-related event, with clearly defined direction and proof source |

The **exact field names and radio ACK semantics remain S1 deliverables**, not assumptions to hard-code now. The ESP32 should forward the packet sequence and radio receive metadata. Do not add unnecessary application-level commands.

### B. Python → dashboard WebSocket format

- Emit the event envelope and field names **already expected by** `schema.js`, `link.js`, and `store.js`.
- Normalize units only if required by the current dashboard schema; document every conversion.
- Preserve original flight packet time (including validity), ground receive time, sequence number, flight mode, and RF metadata as applicable.
- Handle multiple browser clients without one slow/disconnected client blocking serial ingestion or recording.
- On connection, deliver the contractually correct link state if the dashboard supports it; do not fabricate a live flight packet.
- Continue recording when no browsers are connected.

### C. ACK meanings — do not collapse distinct events

These are **different facts**:

1. Ground receiver received flight telemetry.
2. Ground receiver transmitted a radio ACK.
3. Flight payload actually received that radio ACK (requires evidence from payload or a defined protocol mechanism).
4. Python received the ground receiver's serial report.
5. Browser received a WebSocket message.

Only calculate an ACK-success metric from evidence matching that metric's definition. In particular, **ACK transmitted by the ESP32 does not by itself prove ACK received by the STM32**. If the hardware protocol cannot confirm a requested metric, report it as unavailable rather than inventing a success rate.

### D. Session data ownership

- **Payload microSD** stores onboard data independently of the radio link.
- **Ground JSONL** stores packets/events actually observed by the receiver/server, including ground receive timestamps and available RSSI/SNR.
- Missing ground packets cannot be reconstructed without later importing onboard logs.
- JSONL is the internal durable append format; provide a **separate conversion/export** matching the dashboard's *existing* historical JSON importer. Do not assume the importer accepts JSONL.

---

## 4. Implementation stages and approval gates

> **Execution rule:** Finish the current stage and supply concrete test results before starting the next. Do not claim PASS from code inspection, tests not run, or unexamined screenshots. Reviewers must inspect requested screenshots themselves before accepting visual behavior.

### S1 — Contract audit and server foundation

**Tasks**

- [ ] Inspect existing JS schema, WebSocket client, state store, simulator, Data Logs importer, and relevant tests.
- [ ] Document an explicit serial message specification and a mapping to existing dashboard messages (types, fields, data types, units, time, null/error behavior).
- [ ] Resolve the radio ACK definition and distinguish RF delivery, serial delivery, and browser delivery.
- [ ] Define bounded limits: maximum serial line length, per-field validation rules, malformed line handling, read timeout, and reconnect behavior.
- [ ] Create `ground-station/server/`, `requirements.txt`, `README.md`, CLI entry point, and `.gitignore` entry for generated `data/` and Python caches.
- [ ] Ensure default binding is loopback only. No physical hardware required yet.

**Pass criteria / evidence**

- [ ] A reviewed compatibility table: **existing dashboard input** ↔ **Python output** ↔ **ESP32 input**.
- [ ] Setup and CLI commands documented in README.
- [ ] Python module import/syntax validation passes.
- [ ] No dashboard modifications (unless a genuine, demonstrated blocker is explicitly approved).

**Stop point:** Request S1 approval before implementing S2.

### S2 — WebSocket delivery with controlled fixtures

**Tasks**

- [ ] Implement async WebSocket server lifecycle, connection acceptance, broadcast, and graceful client disconnect handling.
- [ ] Implement `protocol.py` validation/translation using the **S1 mappings**.
- [ ] Add a small offline fixture input mode for deterministic integration tests; **do not** build a second full telemetry simulator.
- [ ] Propagate appropriate WebSocket/link states into the existing client contract.
- [ ] Add protocol and WebSocket tests for happy path, malformed/invalid messages, client connect/disconnect, and multi-client fan-out.
- [ ] Verify the existing dashboard Connect/Disconnect flow against the Python server.

**Pass criteria / evidence**

- [ ] Fixture-fed telemetry produces correct altitude, temperature, pressure, GPS, mode, and other mapped fields on the dashboard.
- [ ] Dashboard can disconnect/reconnect; no Python traceback or browser console errors.
- [ ] Two concurrent clients receive the expected messages without preventing continued ingestion.
- [ ] Invalid fixtures are rejected without producing false dashboard telemetry.
- [ ] Capture server/test output and **actual dashboard screenshot(s)**; request that the user attach screenshots for visual review if unavailable.

**Stop point:** Request S2 approval before implementing S3.

### S3 — USB serial ingestion and recovery

**Tasks**

- [ ] Implement configurable port and baud options with documented startup flags.
- [ ] Implement newline framing, UTF-8/JSON parsing, size limit, timeout, and partial-line recovery.
- [ ] Keep serial I/O off the WebSocket event loop; ensure shutdown closes the serial port and worker cleanly.
- [ ] Implement explicit serial link states: unavailable/disconnected, connected, reconnecting (only expose those supported by the dashboard contract).
- [ ] Reconnect with bounded retry/backoff when unplugged, missing, or reset. Do not lock up the WebSocket server.
- [ ] Test with a scripted serial producer or virtual loopback before real ESP32 hardware.

**Pass criteria / evidence**

- [ ] Well-formed serial messages reach dashboard unchanged apart from documented normalization.
- [ ] Malformed JSON, truncated lines, invalid UTF-8, and oversized lines are handled without server crashes.
- [ ] USB disconnect/reconnect resumes live ingestion without restarting Python.
- [ ] Dashboard remains reachable and shows appropriate link-state/stale behavior during loss of serial input.
- [ ] Provide console logs and test output for normal operation and recovery.

**Stop point:** Request S3 approval before implementing S4.

### S4 — Durable ground recording and session export

**Tasks**

- [ ] Write append-only, newline-delimited records for validated observed telemetry/events, including ground receive time and RF metadata when present.
- [ ] Create per-session output files in git-ignored `server/data/`; document session-start/end and filename conventions.
- [ ] Establish a lightweight flush/close policy that makes previously written records recoverable after an abnormal stop; do not claim guaranteed survival of a write that never reached storage.
- [ ] Ensure recording does not depend on an active WebSocket browser connection.
- [ ] Implement a simple exporter to the **exact historical session JSON structure** identified in S1.
- [ ] Add tests for restart, empty/partial last line, malformed input rejection, export correctness, and session separation.

**Pass criteria / evidence**

- [ ] Recorded data exists after a normal restart and earlier complete records can be read following a simulated interrupted write.
- [ ] Exported JSON loads in the dashboard **without any frontend schema change**.
- [ ] Existing Data Logs session selection, date filters, and CSV export work with the newly exported session.
- [ ] Provide a sample sanitized session file, test output, and the actual Data Logs screenshot(s) needed for approval.

**Stop point:** Request S4 approval before implementing S5.

### S5 — ESP32-S3 and full ground integration

**Prerequisites:** ESP32 firmware must emit the agreed serial format and real radio messages. LoRa/payload firmware and hardware testing are **external dependencies**, not responsibilities to hide inside server work.

**Tasks**

- [ ] Connect the real ESP32-S3 over USB with the configured serial port/baud.
- [ ] Inspect and document first received real packets; compare them to the S1 interface contract.
- [ ] Verify real packet sequence, payload measurement values, receiver RSSI/SNR, and radio ACK-related reports.
- [ ] Confirm multi-client dashboard display and simultaneous recording.
- [ ] Exercise lost-radio-packet case, disconnected USB case, browser disconnect, and service restart.
- [ ] Record observed packet gaps/duplicates without inventing data or conflating ESP32 ACK TX with confirmed STM32 ACK RX.
- [ ] Keep changes to the server or interface mapping limited to demonstrated interoperability defects.

**Pass criteria / evidence**

- [ ] Real, numbered flight telemetry travels: **STM32 → LoRa → ESP32 → USB serial → Python → WebSocket → dashboard**.
- [ ] Values and sequence numbers match across source logs, server records, and dashboard.
- [ ] Real RF metrics/ACK states reflect defined hardware evidence; unsupported metrics show unknown/unavailable.
- [ ] Records persist through a browser disconnect and service restart.
- [ ] Capture firmware TX/RX logs, Python logs, test outputs, and **attached actual UI screenshots** for review.

**Stop point:** S5 closes the **ground server integration** workstream; wider flight endurance, range, and thermal validation remain in the separate system-testing plan.

---

## 5. Test matrix (minimum)

| Case | Input/condition | Expected result | First stage |
|---|---|---|---|
| Valid telemetry | Valid fixture message | Existing dashboard receives correct mapped values | S2 |
| Wrong type/range | Missing/invalid required field | Reject/log, don't forward as valid telemetry | S2 |
| Multiple viewers | 2 WebSocket clients | Both receive messages; neither blocks ingest | S2 |
| Browser disconnect | Viewer closes | Ingestion continues; server survives | S2 |
| Serial missing | Startup without configured/available ESP32 | Server remains operational and reports disconnected | S3 |
| Partial/malformed line | Truncated JSON or bad encoding | Safely discard/handle; recover on next valid line | S3 |
| USB disconnect | Remove and reattach device | Automatic serial reconnection | S3 |
| Sequence gap | Packet N followed by N+2 | Gap observable; missing packet not synthesized | S3/S5 |
| ACK ambiguity | ACK sent by ground, no proof received in flight | Do not count as proven flight ACK success | S2/S5 |
| No viewers | Telemetry arrives while all browsers closed | Persist ground records | S4 |
| Unclean record end | Simulated truncated last JSONL line | Previously complete lines recoverable | S4 |
| History import | Exported mission JSON | Existing session importer/filter/CSV works | S4 |
| Physical integration | Real STM32 packets over LoRa | End-to-end telemetry matches hardware logs | S5 |

Automated checks are necessary but not a substitute for **real-device evidence** in S5.

---

## 6. Execution, reporting, and change discipline

At the end of **each** S-stage, report:

1. **What changed:** exact files added/modified, with short description.
2. **Why:** which approved requirement each change satisfies.
3. **How verified:** exact commands, test counts, observed outputs, screenshots or attachment references.
4. **Outcome:** PASS/FAIL per acceptance criterion, with failures and unresolved issues clearly separated.
5. **Next stage readiness:** explicitly request review/approval; do not proceed merely because local tests pass.

Additional rules:

- No subagents; keep implementation sequential and scope-limited.
- Do not modify unrelated dashboard code, committed simulator behavior, or firmware directories.
- Do not claim hardware was tested using synthetic fixtures.
- Never claim screenshot verification unless actual screenshots were inspected. If the necessary UI evidence cannot be seen, request the precise images from the user and leave that criterion **UNVERIFIED**.
- Keep test data clearly distinguishable from live mission telemetry.
- Maintain a readable error log for malformed input and link-state transitions without dumping huge streams to the console.
- Add only changes needed to meet the approved stage's acceptance criteria. Optional improvements go to a later backlog, not into the current stage.
- Commit completed and reviewed stages with scoped, descriptive messages. Do not mix old repository cleanup into these commits.

---

## 7. Stage tracker

| Stage | Status | Review approval | Evidence |
|---|---|---|---|
| S1 — Contracts + foundation | Done | Approved | README.md §4–7; S1 report |
| S2 — WebSocket + fixtures | Done | Approved | 40 tests (test_protocol 28 + test_websocket 12) |
| S3 — Serial ingest + recovery | Complete | Pending | `tests/test_serial.py` (12 tests, requires `pyserial`); S3 verification: 52 tests pass (S2: 40 + S3 serial: 12). Full suite (final): 108 Python tests + 2 Node.js = 110 total |
| S4 — Ground recording + export | Complete | Pending | `tests/test_recorder.py` (54 tests) + `tests/test_e2e.py` (2 end-to-end tests); Node.js tests (2 tests); full Python suite 108 tests (S2: 40 + S3 serial: 12 + S4 recorder: 54 + e2e: 2), 110 total with JS |
| S5 — Physical integration | Not started | Pending | — |

**Next authorized action:** S5 — physical integration (requires real
ESP32-S3/STM32 hardware). S1–S4 implementation and automated verification are
complete (108 Python tests; S3/S4 remain pending formal review sign-off). No
hardware validation is claimed.
