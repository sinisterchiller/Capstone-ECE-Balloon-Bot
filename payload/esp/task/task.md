# GPS Integration Implementation Plan — ESP32-WROOM / ESP-IDF

## Status Summary (2026-11-12)

| Stage | Description | Status |
|-------|-------------|--------|
| 1 | Confirm target and integration constraints | ⚠️ Target/wiring confirmed; receiver settings assumed/unverified |
| 2 | Obtain the official Espressif parser | ✅ Implemented and build-passed |
| 3 | Integrate the parser into ESP-IDF build | ✅ Implemented and build-passed |
| 4 | Add the minimal C++ GPS interface | ✅ Implemented and build-passed |
| 5 | Connect GPS to the application | ✅ Implemented and build-passed |
| 6 | Build, flash, and verify on hardware | ⏳ Pending (hardware not available; unverified) |

**Software stages 2–5 are implemented and build-passed.** Stage 1 target board (ESP32-WROOM), NEO-6M module, and RX GPIO26/GND/3.3V wiring are confirmed, but receiver UART factory defaults (9600 8N1 1 Hz) remain assumed and unverified. Stage 6 requires physical hardware with outdoor sky view; no hardware validation has been performed.

---

## Objective

Integrate a u-blox NEO-6 GPS receiver into the existing ESP-IDF Hello World project using **Espressif's official NMEA0183 parser example**. Keep third-party / imported driver source under `drivers/`. Keep the application-facing `gps.h` and `gps.cpp` minimal; UART access and NMEA parsing must remain inside the imported driver.

## Current repository (from supplied Repomix snapshot)

- `main/main.c`: unmodified ESP-IDF Hello World app.
- `main/CMakeLists.txt`: registers `main.c` with an existing `spi_flash` dependency.
- Root `CMakeLists.txt`: enables `MINIMAL_BUILD ON`.
- `task/task.md`: currently empty.
- Existing README, QEMU/test files, and devcontainer setup should otherwise be left alone.

## Driver origin and files

**Official source:** https://github.com/espressif/esp-idf/tree/master/examples/peripherals/uart/nmea0183_parser

The upstream example's **`main/`** directory contains the files to review:
- `nmea_parser.c` — parser and UART/event handling implementation.
- `nmea_parser.h` — exported parser API and GPS structures.
- `Kconfig.projbuild` — parser settings; inspect and adapt any example-specific references when making it a project component.
- `CMakeLists.txt` — use as a reference, **not unchanged**, because it also compiles the example `nmea_parser_example_main.c`.

Do **not** import `nmea_parser_example_main.c` into the final application. Preserve Espressif copyright/license headers, and record the selected ESP-IDF release or commit when copying the driver. **Pin a release matching the project's installed ESP-IDF version; don't blindly copy floating `master`.**

ESP-IDF already provides the underlying `esp_driver_uart` and `esp_event` components: they are dependencies, not extra downloads.

## Planned project layout

```text
project-root/
├── drivers/
│   └── nmea_parser/
│       ├── nmea_parser.c           # imported from Espressif
│       ├── nmea_parser.h           # imported from Espressif
│       ├── Kconfig.projbuild       # adapted if needed
│       └── CMakeLists.txt          # component registration only
├── main/
│   ├── main.cpp                    # replaces existing main.c
│   ├── gps.h                       # minimal app-facing API
│   ├── gps.cpp                     # thin adapter to imported parser
│   └── CMakeLists.txt
├── task/
│   └── task.md                     # this plan
├── CMakeLists.txt
└── ...                             # preserve existing project files
```

## Staged implementation plan (nothing implemented yet)

Complete each stage and check its **exit criteria** before starting the next. Stop and ask for clarification where a required project or hardware detail is unknown; do not silently choose values.

### Stage 1 — Confirm target and integration constraints

**Goal:** Establish the exact environment and GPS connection before selecting driver files.

**Tasks:**
1. Check the ESP-IDF version used by this repository/devcontainer.
2. Confirm the actual ESP32-WROOM board, NEO-6 module/breakout variant, and connected UART RX GPIO.
3. Confirm UART defaults against the receiver setup; use 9600 baud, 8N1, 1 Hz **only if the module is still configured to its documented defaults**.
4. Note that the existing root CMake file enables `MINIMAL_BUILD ON` and `main/main.c` is a Hello World example.

**Deliverable:** A short record of confirmed ESP-IDF version, hardware variant, wiring, and GPS UART settings.

**Exit criteria:** No unresolved assumptions that affect driver selection or wiring. Ask the user rather than guessing if details cannot be established.

### Stage 2 — Obtain the official Espressif parser

**Goal:** Put the upstream driver source in `drivers/` without rewriting it.

**Tasks:**
1. Select an Espressif NMEA0183 parser source revision aligned with the confirmed ESP-IDF version; record its tag or commit.
2. Take `nmea_parser.c`, `nmea_parser.h`, and any required parser configuration from the official example.
3. Place the required files under `drivers/nmea_parser/`; keep copyright/license notices.
4. Exclude the example's demo `nmea_parser_example_main.c`.
5. Review upstream build/configuration dependencies; don't add optional files unless the selected revision actually requires them.

**Deliverable:** A locally stored official NMEA parser in `drivers/nmea_parser/`, with source revision documented.

**Exit criteria:** All imported code is traceable to Espressif; no custom UART or NMEA implementation added.

### Stage 3 — Integrate the parser into ESP-IDF build

**Goal:** Make the imported parser available to the application as a project component.

**Tasks:**
1. Register `drivers/` with `EXTRA_COMPONENT_DIRS` in the root `CMakeLists.txt` before `project(...)`.
2. Create/adapt `drivers/nmea_parser/CMakeLists.txt` to compile the parser only and declare the ESP-IDF dependencies required by the selected revision (e.g., UART and event components).
3. Verify parser configuration is accessible, including its existing UART RX pin setting.
4. Ensure the application declares a dependency on `nmea_parser`, accounting for `MINIMAL_BUILD ON`.

**Deliverable:** Parser component recognized by CMake and available to `main`.

**Exit criteria:** ESP-IDF configuration and component dependency resolution succeed without compiling the upstream example application.

### Stage 4 — Add the minimal C++ GPS interface

**Goal:** Keep application GPS integration thin and readable.

**Tasks:**
1. Add `main/gps.h` with only the application-facing declarations needed to initialize GPS and retrieve its latest status/position.
2. Add `main/gps.cpp` to initialize Espressif's parser, subscribe to its GPS update events, and expose the most recent data.
3. Prefer the parser's existing types and event API where practical; add no generic driver framework or duplicate NMEA decoder.
4. Make any access to asynchronously updated data safe if read from another task.

**Deliverable:** Minimal `gps.h` / `gps.cpp` adapter, with all UART and NMEA logic still inside `drivers/nmea_parser/`.

**Exit criteria:** `gps.h` and `gps.cpp` contain no low-level UART reads, NMEA framing, checksum code, or custom GPS receive task.

### Stage 5 — Connect GPS to the application

**Goal:** Replace Hello World with the smallest possible GPS demonstration.

**Tasks:**
1. Rename `main/main.c` to `main/main.cpp` and retain the ESP-IDF `app_main` C linkage.
2. Update `main/CMakeLists.txt` to compile the C++ entry point and `gps.cpp`, and link the parser component.
3. Initialize GPS once and log fix validity and latitude/longitude when a valid update is available.
4. Leave unrelated repository structure, tests, and devcontainer files alone unless directly affected by the entry-point change.

**Deliverable:** A simple C++ application using only the public API in `gps.h`.

**Exit criteria:** No parser/UART implementation leaks into `main.cpp`, `gps.h`, or `gps.cpp`.

### Stage 6 — Build, flash, and verify on hardware

**Goal:** Validate the complete integration on an actual ESP32-WROOM and NEO-6 receiver.

**Tasks:**
1. Configure/build for target `esp32`; resolve any dependency or API differences against the selected ESP-IDF revision.
2. Flash and monitor the ESP32-WROOM.
3. Verify the receiver reports the initial no-fix state accurately; outdoors, confirm that a valid fix produces plausible coordinates.
4. Verify receiver disconnect/reconnect and ESP32 restart behavior.
5. Check that existing tests or build checks affected by the `main.c` → `main.cpp` rename are updated only as necessary.

**Deliverable:** Demonstrated GPS readout and documented test results.

**Exit criteria:** Every acceptance criterion below passes; any blocked hardware-dependent check is recorded as **not verified**, not assumed successful.

## Hardware assumption — verify before implementation

- Target: ESP32-WROOM board, with NEO-6M-compatible GPS receiver.
- Initial connection is one-way UART: **NEO-6 TX → ESP32 RX** and common GND; UART TX from ESP32 is not needed for passive NMEA reception.
- **Proposed** ESP32 RX pin: GPIO26, configurable via the parser; confirm against the actual board's wiring.
- A bare NEO-6M is a 2.7–3.6 V device; a GPS breakout's labeled VCC pin may have different requirements. Verify the specific breakout before powering it.

## Acceptance criteria

- [ ] Imported parser resides under `drivers/nmea_parser/`; source/version and license retained.
- [ ] `main.cpp` is the C++ entry point and the ESP-IDF project builds for ESP32-WROOM.
- [ ] `gps.h` and `gps.cpp` contain no raw UART or NMEA parser implementation.
- [ ] The application receives GPS updates using the official parser's API.
- [ ] Fix status is respected; invalid/no-fix data is not presented as a valid position.
- [ ] A real NEO-6 GPS receiver produces positions in the serial monitor outdoors.
- [ ] Original unrelated project files and tests remain intact or are adjusted only if the C-to-C++ rename requires it.

## Out of scope

UBX configuration, 5 Hz tuning, PPS, advanced power modes, custom NMEA parsing, MQTT, cloud logging, and introducing additional GPS tasks or abstraction layers.

## Reference material

- Espressif NMEA parser example: https://github.com/espressif/esp-idf/tree/master/examples/peripherals/uart/nmea0183_parser
- Example parser header: https://github.com/espressif/esp-idf/blob/master/examples/peripherals/uart/nmea0183_parser/main/nmea_parser.h
- Example parser source: https://github.com/espressif/esp-idf/blob/master/examples/peripherals/uart/nmea0183_parser/main/nmea_parser.c
- ESP-IDF build system: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
- u-blox NEO-6 datasheet supplied with this conversation: default NMEA/UART parameters on page 22; module voltage specifications on page 15.
