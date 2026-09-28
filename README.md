# ECE Balloon Bot — Project Summary

## 1. Project overview

- **Client:** Prof. Yize Chen (yize9@ualberta.ca)
- **Project title:** ECE Balloon Bot
- **Nature:** An exploratory capstone focused on building a **reliable, autonomous robotic atmospheric-sensing platform**, rather than merely constructing a conventional weather balloon.
- **Primary goal:** Design a lightweight, low-power balloon payload that detects atmospheric events in real time; autonomously adapts its sensing, onboard computation, and communications; and completes a balloon mission with minimal human intervention while recording a high-quality **vertical atmospheric profile**.
- **Core system:** Meteorological sensors, onboard state estimation, edge computing, wireless telemetry, energy monitoring, and autonomous decision-making, integrated into a lightweight payload.
- **Student deliverables:** Payload hardware, embedded software, autonomous sensing algorithms, a ground-station interface/dashboard, and environmental test procedures.

## 2. Flight and communications

| Topic | Current guidance / target |
|---|---|
| Initial altitude | **30–100 m above ground**, subject to site and aviation clearance. |
| Optional high-altitude objective | **10–12 km above sea level**, as a separate stretch goal after a flight-readiness review. Both altitude figures are design targets, not approved flight permissions. |
| Communication range and mode | No fixed minimum range or radio technology yet; choose based on budget and tests of different frequencies and devices. |
| Flight control | **No propulsion, steering, or active altitude control required.** |
| Preventing loss | Recovery cannot be guaranteed. Begin with bench tests, ground communication tests, and an **approved low-altitude tethered test**. For any later free flight, provide GPS tracking, onboard data storage, a trajectory-prediction and recovery plan, and an appropriate descent system. |
| Regulatory note | Prof. Chen's preliminary view: Canadian Aviation Regulation **602.42** requires authorization for an unoccupied free balloon with gas-carrying capacity exceeding **115 ft³**, and the project should fall below that threshold. **Site/aviation clearance and any other applicable requirements still need to be checked before flight; this is not a blanket launch approval.** |
| Illustrative flight data | [Example past balloon tracking records](https://traquito.github.io/search/spots/dashboard/?band=20m&channel=185&callsign=KD2KDD&dtGte=2024-03-22&dtLte=2024-04-04) shared by Prof. Chen. |

**Unresolved:** Exact communications range, frequencies/devices, test/launch locations, authorization requirements in the specific flight scenario, and who covers any regulatory or flight-related costs (project budget, university, or students).

## 3. Payload and embedded system

| Topic | Requirement / guidance |
|---|---|
| Size | Tentative envelope **within 20 × 20 × 20 cm**, adjustable according to device selection and design. |
| Mass | Target **under 500 g**. |
| PCB | A self-designed/custom PCB is **recommended**, particularly if it improves integration, mass, or reliability; not explicitly stated to be mandatory. |
| Operating temperature | Tentative rough design range **−20 °C to +40 °C**; not finalized. |
| Sensors | Measure atmospheric temperature, relative humidity, pressure, **position, altitude, and balloon motion**. |
| Autonomy | Use onboard state estimation, edge computing, and decision-making to adjust sampling and communication based on estimated **scientific value**. |
| System health | Monitor **battery, thermal, sensor, and communication health** throughout the mission. |
| Data handling | Record atmospheric measurements and a vertical profile; include onboard data storage for a subsequent free flight and real-time telemetry where supported. |

## 4. Sensing and adaptive behavior

- Detect atmospheric events **in real time** and adapt sensing, computation, and wireless transmission accordingly.
- **Example from Prof. Chen:** If a sustained temperature or humidity change exceeds a selected threshold, increase sampling to **once per second** and flag the event. Otherwise, send data actively **only when necessary** to reduce unnecessary communication.
- The adaptive system should consider the estimated scientific value of measurements, rather than simply sampling/transmitting at a fixed high rate.
- **Still unspecified:** Which atmospheric events must be detected; required accuracy of each weather measurement; precise definitions/algorithms for event detection, threshold selection, adaptive sampling, and decision-making. The professor's threshold example is a starting concept, not a finalized algorithm or threshold.

## 5. Ground station and telemetry

- Build a ground-station dashboard providing **real-time telemetry and visualization**.
- **Minimum starting display:** Ambient temperature, pressure, relative humidity, GPS position/altitude, and power status.
- Support reception and display of radio-transmitted sensor data and logging.
- Communication health must be monitored as part of the overall mission-health objective, but **the exact link-health metrics, reporting method, and dashboard sophistication are not yet defined**. The professor's response to the communication-health question primarily described event-triggered sampling and transmission, not a specific link-health protocol.
- An initial **functional laptop dashboard in the lab** is sufficient; further features can be developed iteratively.

## 6. Testing, validation, and final demonstration

1. **Laboratory/bench testing:** Test sensors, embedded computation, power, logging, and the integrated payload. Prof. Chen's group has some lab space available.
2. **Ground communication tests:** Verify radio transmission/reception and ground-station integration.
3. **Environmental testing:** Validate operation against relevant environmental conditions, including the tentative temperature target.
4. **Approved low-altitude tethered testing:** Test sensing and telemetry before considering free flight; outdoor field tests may be required.
5. **Flight testing:** Validate the complete system and atmospheric-profile recording if flight readiness and necessary clearances permit it; a later high-altitude mission is a separate stretch goal.
6. **Final presentation:** Prof. Chen currently envisions a **live demonstration of the integrated payload and dashboard at the university**, showing sensor readings, radio transmission, and logging. The demonstration need not itself be an outdoor balloon flight.

## 7. Success criteria and client priorities

- Prof. Chen frames the project as **exploratory training**: learning new skills and investigating novel topics are central outcomes.
- Successful progress on multiple key submodules would be a positive result: **communication, PCB design, sensing, balloon testing, test flight, and data dashboard**.
- Desired end-to-end outcome: a reliable autonomous payload and ground station that operate with minimal intervention and produce useful atmospheric measurements and a high-quality vertical profile.
- **Client perspective:** Prof. Chen currently considers himself the sole client; he wants experience with a “flying bot” and interesting research developments around the balloon system.

## 8. Open decisions and assumptions to confirm

- Exact atmospheric-event definitions, sensor accuracy requirements, and autonomy/decision-making algorithms.
- Communication technology, operating frequencies, tested range, update rate, and concrete communication-health metrics.
- Final payload dimensions, weight allocation, and validated environmental operating range.
- Whether a custom PCB becomes a formal requirement.
- Permitted test/launch sites, aviation and site clearances for each flight configuration, and responsibility for any related costs.
- Scope of outdoor/free-flight testing versus an indoor live final demonstration; whether the optional 10–12 km mission will be attempted.
- Dashboard feature scope beyond the initial readings, GPS, power, radio reception, and logging.

> **Status note:** Numerical limits and plans above reflect the project description and Prof. Chen's preliminary feedback. Several are tentative targets or stretch objectives rather than finalized requirements or verified regulatory approvals.
