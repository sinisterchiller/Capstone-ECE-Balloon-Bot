# Project memory — ECE Balloon Bot

## Purpose
ECE 490 capstone (Group 13): autonomous weather balloon payload + ground station.
Client: Prof. Yize Chen. Flight targets: 30–100 m initial test flights; design for 10–12 km.

## Key facts (from README.md)
- Payload: STM32 + MS5611 (pressure, 10–110 kPa), SHT45 (humidity), LSM6DSO32 (IMU), PT103J2 (temperature), SAM-M10Q (GNSS), microSD, LAMBDA62C-9S LoRa 915 MHz.
- Ground: ESP32-S3 LoRa RX → laptop Python ingestion → web dashboard.
- Modes (autonomous, STM32-controlled): Normal 5 s, Event 1 s, Low Power 20 s sampling.
- Component temperature rating: −60 to 40 °C. Battery: ≥3 h operation; voltage reported ≤10 min intervals with low-battery flag.
- Target: ≥95% packet delivery during testing.
- GNSS: 1 s position/altitude recording when 3D fix available.

## Current state (2026-10-21)
- `interface/plan.md` written (full dashboard spec).
- No dashboard code yet; `server/` and `esp/` empty.
