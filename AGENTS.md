# AGENTS.md — ECE Balloon Bot

Capstone project (ECE 490, Group 13): autonomous weather balloon payload + ground station.

## Architecture (confirmed in README.md)

- **Flight payload (STM32):** MS5611 (pressure), SHT45 (humidity), LSM6DSO32 (IMU), PT103J2 (temperature), SAM-M10Q (GNSS) → filtering, event detection, adaptive sampling → microSD logging + LoRa (LAMBDA62C-9S, 915 MHz).
- **Ground station (ESP32-S3):** LoRa RX → laptop Python ingestion/processing → **web dashboard** (`ground-station/interface/`).
- Operating modes are set autonomously by the STM32: Normal (5 s sampling), Event (1 s), Low Power (20 s). The dashboard is for monitoring, analysis and logging only — it does not control the balloon.

## Key directories

- `ground-station/interface/` — web dashboard (vanilla HTML/CSS/JS, no build step)
- `ground-station/server/` — Python telemetry ingestion (not started)
- `ground-station/esp/` — ESP32-S3 firmware (not started)
- `datasheets/`, `hardware-requirements/` — component documentation
- `README.md` — full project report (requirements, constraints, trade-offs)

## Dashboard constraints (from `ground-station/interface/plan.md`)

- Dark mission-control theme, laptop 1920×1080, seven pages: Overview, GPS Tracking, Atmospheric Data, Event Analysis, Communication, Data Logs, Settings.
- Real-time updates as packets arrive (no page refresh); stale data greyed out with time-since-last-packet.
- Must work offline in the field: **no CDN dependencies, no tile servers, no npm/build tooling**.
- Monospaced numerals for telemetry/timestamps; cyan/blue accent; green/amber/red status colors.
- Humidity is internal/enclosure humidity (SHT45 not rated for −60 °C ambient) — label it as such.

## Commands

- Dashboard: serve `ground-station/interface/` over HTTP (e.g. `python3 -m http.server 8000` from that directory) and open in a browser. No build step.

## Conventions

- Vanilla JS, ES modules, no frameworks, no package manager.
- Keep files small and purpose-named.
- `.claude/memory/` holds detailed decisions/progress; keep it compact and current.
