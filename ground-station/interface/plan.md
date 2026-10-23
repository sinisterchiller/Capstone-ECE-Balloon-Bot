The ECE Balloon Bot Ground Station Dashboard should use a clean, technical mission-control layout, optimized for a laptop screen (1920 × 1080). It should prioritize real-time monitoring, GPS tracking, atmospheric analysis, and communication reliability.

## 1. Overall layout

Use a dark-themed dashboard with a fixed sidebar, top status bar, and scrollable main content area.

BALLOON BOT

GROUND CONTROL

Overview

GPS Tracking

Atmospheric Data

Event Analysis

Communication

Data Logs

Settings

Mission Overview

LINK ● | NORMAL

ALTITUDE

### —

TEMPERATURE

### —

PRESSURE

### —

BATTERY

### —

VERTICAL SPEED

### —

MODE

### —

GPS Flight Map

Atmospheric Graphs

Communication Health

Awaiting telemetry

Event Monitor

Awaiting telemetry

Recent Telemetry / System Logs

Timestamp | Packet ID | Type | Status

Conceptual wireframe of the default Overview page. Actual sections should be larger on the laptop display.

## 2. Page-by-page layout

### Sidebar — Fixed left navigation

Approximately 220–250 pixels wide, always accessible, with seven destinations:

| Page             | Main purpose                                                           |
| ---------------- | ---------------------------------------------------------------------- |
| Overview         | Everything important at a glance                                       |
| GPS Tracking     | Balloon location, flight path, altitude and speed                      |
| Atmospheric Data | Sensor graphs, vertical atmospheric profiles and historical comparison |
| Event Analysis   | Detected anomalies, event timelines and mode transitions               |
| Communication    | LoRa signal strength, packet delivery and ACK diagnostics              |
| Data Logs        | Recorded telemetry, session history and CSV export                     |
| Settings         | Serial connection, dashboard preferences and system configuration      |

### Top bar — Persistent system status

Across the main content area, show the mission name, elapsed mission time, last packet received, current operating mode, LoRa connection status and battery condition.

The most important design rule is that connection status and stale telemetry warnings remain visible on every page.

### Overview — Main dashboard

Use a three-level hierarchy:

1. Top: Six telemetry cards displaying GPS altitude, external temperature, atmospheric pressure, battery, vertical speed and payload mode.
2. Middle: A large GPS map on the left, occupying roughly 55% of the available width, and live atmospheric graphs on the right, occupying 45%.
3. Bottom: Communication-health panel and event-monitor panel side by side, followed by a full-width recent telemetry log.

This layout makes the balloon's location and atmospheric behavior the primary visual focus.

### GPS Tracking page

The map occupies approximately 70% of the page. A narrower right-side information panel displays the latest coordinates, altitude, vertical speed, GNSS fix status, last-known location and packet timestamp.

Show the flight path as a connected trace, with distinct markers for launch position, current position and last valid position.

### Atmospheric Data page

Arrange selectable graphs in a two-column grid. Include external temperature versus time, pressure versus time, altitude versus time and vertical speed versus time.

Below these, include vertical atmospheric profiles such as temperature versus altitude and pressure versus altitude. Provide time-range selection and a way to distinguish live data from historical sessions.

Label humidity as internal/enclosure humidity unless an externally representative measurement is available.

### Event Analysis page

Use a chronological event timeline as the central element. Selecting an event opens a detail panel showing the event timestamp, type, triggering measurement, threshold, operating mode and related sensor readings.

Include a graph around the event so users can inspect sensor behavior before and after the trigger.

### Communication page

Place four key diagnostics across the top: RSSI, SNR, packet delivery percentage and ACK success.

Underneath, show signal-quality graphs over time, followed by a packet table containing sequence number, receive timestamp, packet type, validation result and acknowledgement status.

### Data Logs page

Use a full-width searchable telemetry table with timestamp, GPS position, temperature, pressure, battery level, operating mode and packet sequence number.

At the top, provide session selection, date/time filters and CSV export. Distinguish data received on the ground from onboard microSD records that have not been recovered or imported.

## 3. Visual style

The design should be technical and readable rather than flashy.

- Background: Dark navy or charcoal.
- Panels: Slightly lighter dark-grey surfaces with subtle borders.
- Typography: Clean sans-serif text, with monospaced numerals for telemetry and timestamps.
- Accent: Cyan or blue for active charts, selected navigation and live telemetry.
- Status colors: Green for nominal operation, amber for warnings and red for faults.
- Spacing: Consistent 16–24 px gaps, compact navigation and generously sized graph panels.

## 4. Interaction behavior

Data should update automatically as packets arrive from the ESP32-S3, without refreshing the page. Telemetry cards update immediately; graphs append new samples; the map updates the balloon's position; and events appear in the timeline.

When the radio connection is lost, keep the most recent measurement visible, but grey it out and show the time since the last received packet. Avoid displaying stale measurements as though they are live.

The STM32 remains responsible for autonomous mode changes and event detection. The dashboard is primarily for monitoring, analysis and logging—not manually controlling the balloon.

Overall: Build the Overview page as the mission's central screen, with GPS and atmospheric trends taking the most space, while the other pages provide deeper analysis without cluttering the main display.
