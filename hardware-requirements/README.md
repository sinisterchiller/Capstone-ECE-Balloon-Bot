# ECE Balloon Bot — Hardware Requirements and Current Component Selection

**Project:** ECE Balloon Bot  
**Client:** Prof. Yize Chen  
**Document status:** Preliminary hardware requirements with current proposed component selections  

This document reflects the current capstone scope: an autonomous, lightweight, low-power atmospheric-sensing payload, a wireless ground station, and controlled testing. It distinguishes required functionality from currently selected hardware and optional flight objectives.

The current architecture uses an **ESP32-S3 controller**, **915 MHz LoRa telemetry**, environmental sensing, GNSS positioning, inertial sensing, onboard storage, and power/health monitoring. The selected modules are intended to be usable during development and carried forward into the final integrated system wherever practical.

---

## 1. System-Level Design Targets

| Parameter | Current target or guidance |
|---|---|
| Initial test altitude | 30–100 m above ground, subject to site and aviation clearance |
| Optional high-altitude objective | 10–12 km above sea level; separate stretch goal after flight-readiness review |
| Payload dimensions | Tentatively within 20 × 20 × 20 cm |
| Payload mass | Target under 500 g |
| Operating temperature | Tentative design range of −20 °C to +40 °C; not yet validated |
| Required measurements | Atmospheric temperature, relative humidity, pressure, position, altitude, and balloon motion |
| Autonomous operation | Onboard event detection, state estimation, adaptive sensing, adaptive communication, and energy/health monitoring |
| Flight control | No propulsion, steering, or active altitude control required |
| Final demonstration | Live integrated payload and laptop dashboard demonstration at the university; outdoor balloon flight is not necessarily required |

> **Status:** Altitudes, dimensions, mass, environmental range, and budget are preliminary targets, not finalized component-level requirements or flight permissions.

---

## 2. Current Selected Core Hardware

These are the current proposed parts for the sensing and communication architecture. They are intended to be reused in the final build rather than treated as disposable prototype-only hardware.

| Function | Selected component | Qty. | Status / notes |
|---|---|---:|---|
| Flight controller | **Freenove ESP32-S3 Development Board** | 1 | Selected; main controller |
| Ground controller | **Freenove ESP32-S3 Development Board** | 1 | Selected; same platform as flight side |
| LoRa radio | **Adafruit RFM95W 868/915 MHz LoRa Radio Transceiver Breakout — Product 3072** | 2 | Selected; one payload, one ground station |
| LoRa antenna | **TE Connectivity Linx ANT-916-CW-RCS-SMA** | 2 | Selected; 915 MHz external whip antenna |
| LoRa antenna connector | **RF Solutions CON-SMA-EDGE-S** | 2 | Selected; SMA female edge-launch connector for RFM95W |
| Environmental sensor | **Adafruit BME280 Breakout — Product 2652** | 1 | Selected; temperature, humidity, pressure |
| GNSS | **u-blox SAM-M10Q-00B** | 1 | Selected candidate; bare surface-mount GNSS module with integrated antenna |
| IMU | **MPU-6050 6-axis module** | 1 | Selected candidate; Amazon listing is MPU-6050, not MPU-6500 |
| Onboard storage | MicroSD card + interface | 1 | With ESP Module |
| Battery | TBD | 1 | **Still to select** |
| Power regulation | TBD | 1 set | **Still to select** |
| Battery / power monitor | INA219 / INA226-class device or equivalent | 1 | **Still to select** |

### Current Purchase Links

- **[LoRa — Adafruit RFM95W](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/3072/6005357)**

- **[ESP32-S3 — Freenove](https://www.amazon.ca/Freenove-ESP32-S3-Dual-core-Microcontroller-Wireless/dp/B0FRRJCDHF/ref=sr_1_7?tag=snxca56-20&th=1)**

- **[LoRa Antenna — TE Connectivity ANT-916-CW-RCS-SMA](https://www.digikey.ca/en/products/detail/te-connectivity-linx/ANT-916-CW-RCS-SMA/16630288)**

- **[Antenna Connector — RF Solutions CON-SMA-EDGE-S](https://www.digikey.ca/en/products/detail/rf-solutions/CON-SMA-EDGE-S/5845767)**

- **[Humidity / Temperature / Pressure — Adafruit BME280](https://www.digikey.ca/en/products/detail/adafruit-industries-llc/2652/5604372)**

- **[GNSS — u-blox SAM-M10Q-00B](https://www.digikey.ca/en/products/detail/u-blox/SAM-M10Q-00B/16672678)**

- **[IMU — MPU-6050 Module](https://www.amazon.ca/Acceleration-Sensors-Accelerometer-Three-Axis-Quadcopter/dp/B07V67DQ5N/ref=sr_1_6?crid=21Y04PTOO8F83&dib=eyJ2IjoiMSJ9.EV9aEqilTAbMihj9UjFSzRVe8IdjJsJmI_p-T_aRNLUPsThrG5bKyQmzs0tIkL--rSxMVCZw2jdr2u--3hIQlDrvjpFbNlOc0xjm4r7eilLE7iv5jnOMo2MsUVvWCBwDoF95EXO83xkyJg_EDAkm5_Inzql6qx_JWnhKCmr2JpxFSDmwwUvFPMSBR6LvNgw3kqUBLdkyLIkgeK0p5CHVvbF4nh2gs_Xe1d_Iljb4AEPAOz_wNIKDR0AQRVUs5Y3zBznn7MUqTUEVpt48GPzfZn5f6inl2DLk1UrGPkc2-Ro.hYpnyQIckpv80_Hha9C2dm_EMwrGkD3iqVnljh7IKF0&dib_tag=se&keywords=mpu6500&qid=1790614599&sprefix=mpu6500%2Caps%2C177&sr=8-6&th=1)**

---

## 3. Proposed Flight Electronics Architecture

```text
                                  ┌─────────────────────────┐
                                  │       BME280            │
                                  │ Temp / Humidity / Press │
                                  └────────────┬────────────┘
                                               │ I²C
                                               ▼
┌──────────────┐      UART / I²C        ┌──────────────┐        I²C       ┌──────────────┐
│  SAM-M10Q    ├────────────────────────►              ◄──────────────────┤ Power /      │
│  GNSS        │                        │              │                  │ Battery Mon. │
└──────────────┘                        │  ESP32-S3    │                  └──────────────┘
                                        │              │
┌──────────────┐          I²C           │              │
│  MPU-6050    ├────────────────────────►              │
│  IMU         │                        └──────┬───────┘
└──────────────┘                               │
                                  ┌────────────┴────────────┐
                                  │                         │
                                  │ SPI                     │ SPI / SDIO
                                  ▼                         ▼
                         ┌────────────────┐          ┌─────────────┐
                         │ Adafruit       │          │  MicroSD    │
                         │ RFM95W         │          │  Storage    │
                         └───────┬────────┘          └─────────────┘
                                 │
                                 │ RF
                                 ▼
                        ┌──────────────────┐
                        │ CON-SMA-EDGE-S   │
                        └────────┬─────────┘
                                 │
                                 ▼
                        ┌──────────────────┐
                        │ 915 MHz Antenna  │
                        │ ANT-916-CW-RCS   │
                        └──────────────────┘

Battery → Protection / Regulation → ESP32 + LoRa + GNSS + Sensors + Storage
```

> **Power-monitoring direction:** the battery/power monitor measures the supply and reports voltage/current/power data back to the ESP32 over I²C; it is not a downstream load controlled by the ESP32.

---

## 4. Flight Payload Hardware Requirements

| ID | Hardware | Qty. | Requirements and purpose | Current status |
|---|---|---:|---|---|
| H-01 | ESP32-S3 development board | 1 | Main controller for sensor acquisition, onboard state estimation, event detection, adaptive sampling, and telemetry management. | **Selected — Freenove ESP32-S3** |
| H-02 | LoRa transceiver | 1 | Wireless communication with the ground station using the selected 915 MHz radio architecture. | **Selected — Adafruit RFM95W** |
| H-03 | LoRa antenna | 1 | Antenna matched to the selected radio frequency and RF connector. | **Selected — TE ANT-916-CW-RCS-SMA** |
| H-04 | LoRa antenna connector | 1 | Provide a 50 Ω SMA interface between the RFM95W and external antenna. | **Selected — CON-SMA-EDGE-S** |
| H-05 | Atmospheric temperature sensor | 1 | Measure ambient air temperature for the vertical atmospheric profile and event detection. | **Selected — BME280** |
| H-06 | Relative humidity sensor | 1 | Measure atmospheric humidity and detect sustained changes. | **Selected — BME280** |
| H-07 | Barometric pressure sensor | 1 | Measure atmospheric pressure and support barometric altitude estimation. | **Selected — BME280** |
| H-08 | GPS/GNSS module | 1 | Measure geographic position, altitude, time, and movement; support mission tracking. | **Selected candidate — SAM-M10Q** |
| H-09 | Inertial measurement unit | 1 | Measure 3-axis acceleration and angular velocity to characterize payload movement, rotation, and launch/descent dynamics. | **Selected candidate — MPU-6050** |
| H-11 | Battery | 1 | Independently power sensing, processing, storage, GNSS, and radio throughout the mission. | **Required — TBD** |
| H-12 | Power regulation and distribution | 1 set | Provide stable supply rails for the controller, sensors, GNSS receiver, storage, and radio. | **Required — TBD** |
| H-13 | Battery monitoring circuit | 1 | Measure voltage and/or current to support energy estimation and autonomous power management. | **Required — TBD** |
| H-14 | System temperature monitoring | 1 | Monitor electronics or battery thermal health separately from ambient atmospheric temperature if required. | **Required functionality — TBD** |
| H-15 | Payload enclosure | 1 | Lightweight protection, secure mounting, appropriate sensor exposure, GNSS sky view, and antenna placement. | 3D Print |
| H-16 | Custom PCB / carrier PCB | 1 | Integrate or mechanically mount controller, radio, GNSS, storage, power circuitry, and connectors into a reliable final assembly. | **Planned** |
| H-17 | Wiring, connectors, and mounting hardware | 1 set | Secure electrical and mechanical integration. | **Required — TBD** |

### Environmental Sensor Placement

The BME280 should preferably be positioned **outside the main electronics enclosure or thermally isolated from the ESP32, regulator, and LoRa radio** so that its temperature measurement represents ambient air rather than PCB self-heating.

A practical arrangement is an external daughterboard connected to the main PCB using a small locking connector:

```text
External BME280
    │
    │ 3V3 / GND / SDA / SCL
    ▼
Locking 4-pin connector
    │
    ▼
Main PCB / ESP32-S3
```

The final enclosure should expose the BME280 to airflow while shielding it from direct solar heating as much as practical.

### GNSS Integration Note

The **SAM-M10Q-00B is a surface-mount module**, not a breadboard breakout. It is therefore best treated as a component to be mounted directly on the final custom PCB. Its integrated GNSS antenna should be positioned with a clear view of the sky and kept away from large conductive objects, batteries, and other RF hardware where practical.

---

## 5. Ground-Station Hardware

| ID | Hardware | Qty. | Requirements and purpose | Current status |
|---|---|---:|---|---|
| G-01 | ESP32-S3 development board | 1 | Receive radio telemetry and forward it to the ground-station computer over Wi-Fi or USB. | **Selected — Freenove ESP32-S3** |
| G-02 | LoRa transceiver | 1 | Receive telemetry from the payload using the same radio family and configuration. | **Selected — Adafruit RFM95W** |
| G-03 | LoRa antenna | 1 | 915 MHz antenna matched to the selected radio and connector. | **Selected — TE ANT-916-CW-RCS-SMA** |
| G-04 | LoRa antenna connector | 1 | SMA interface between radio and antenna. | **Selected — CON-SMA-EDGE-S** |
| G-05 | Ground-station computer | 1 | Run the Python backend, store incoming telemetry, and present the web dashboard. Existing laptop is sufficient. | **Existing equipment acceptable** |

### Initial Dashboard Data

The ground station should initially display and log:

- Atmospheric temperature
- Relative humidity
- Atmospheric pressure
- GPS latitude and longitude
- GPS/GNSS altitude
- Barometric altitude estimate
- Payload acceleration / motion state
- Battery voltage / current / power status
- LoRa RSSI
- LoRa SNR
- Packet sequence / packet-loss information
- Time since last valid telemetry packet

---

## 6. Communications Architecture

### Flight Side

```text
ESP32-S3
    │ SPI
    ▼
Adafruit RFM95W
    │
    ▼
CON-SMA-EDGE-S
    │
    ▼
TE ANT-916-CW-RCS-SMA
```

### Ground Side

```text
ESP32-S3
    │ SPI
    ▼
Adafruit RFM95W
    │
    ▼
CON-SMA-EDGE-S
    │
    ▼
TE ANT-916-CW-RCS-SMA
```

The same radio and antenna architecture on both ends simplifies firmware development, testing, spares, and link-budget validation.

The antennas do **not** need to point directly at one another. For whip antennas, maintaining roughly similar polarization is more important. A vertical or near-vertical installation is preferable where mechanically practical.

---

## 7. Test and Flight Hardware

| ID | Hardware | Qty. | Requirements and purpose | Priority |
|---|---|---:|---|---|
| T-01 | Breadboards and jumper wires | 1 set | Initial sensor/controller/radio integration before final carrier PCB assembly. | Useful for development |
| T-02 | USB programming cables | 2 | Program, debug, and power both ESP32-S3 controllers during development. | Required |
| T-03 | Multimeter | 1 | Check supply voltages, current consumption, and electrical connections. | Required; lab equipment acceptable |
| T-04 | Adjustable laboratory power supply | 1 | Evaluate power consumption, low-voltage behavior, and power distribution. | Recommended; lab equipment acceptable |
| T-05 | Environmental test equipment | As available | Validate sensors, electronics, GNSS, radio, and battery performance across the eventual operating-temperature requirement. | Required for environmental validation |
| T-06 | Balloon and lifting gas | 1 set | Provide adequate lift during an approved tethered test or subsequent flight. | Required for balloon testing |
| T-07 | Tether and anchoring equipment | 1 set | Secure the balloon during approved low-altitude tests. | Required for tethered testing |
| T-08 | Descent and recovery system | 1 set | Enable appropriate descent and recovery for a subsequent free flight. | Required for free flight |
| T-09 | Ground tracking and recovery equipment | As needed | Locate and recover the payload after a free flight. | Required for free flight |

A drone-carried sensor-payload test remains optional, subject to payload capacity, applicable drone rules, and site permissions.

---

## 8. Hardware Support for Autonomous Operation

| Autonomous function | Hardware dependency | Intended operation |
|---|---|---|
| Atmospheric-event detection | BME280 + ESP32-S3 | Detect sustained atmospheric changes using algorithms to be defined and validated. |
| Adaptive sampling | Sensors + ESP32-S3 | Increase or reduce sensor sampling according to mission state or detected events. |
| Adaptive communication | ESP32-S3 + RFM95W | Adjust telemetry behavior according to event value, link state, and available energy. |
| Altitude estimation | BME280 + SAM-M10Q | Combine barometric pressure with GNSS altitude for vertical-profile generation. |
| Motion estimation | MPU-6050 + ESP32-S3 | Detect movement, acceleration, rotation, launch/descent dynamics, and possible tumbling. |
| Energy-aware operation | Battery monitor + ESP32-S3 | Estimate available energy and alter operation when appropriate. |
| Thermal-health monitoring | Electronics/battery temperature sensing | Detect conditions that may affect reliability or battery performance. |
| Communication-health monitoring | RFM95W + ESP32-S3 | Monitor RSSI, SNR, packet loss, and time since last valid communication. |
| Autonomous data logging | ESP32-S3 + MicroSD | Preserve measurements and detected events during radio outages. |

---

## 9. Remaining Critical Hardware Decisions

The core **controller, radio, antenna, environmental sensing, GNSS, and inertial sensing** architecture is now substantially defined. The following items still need to be selected before the flight electronics can be considered complete.

### 9.1 Power System

Required decisions:

- Battery chemistry
- Battery capacity
- Nominal battery voltage
- Low-temperature performance
- Protection circuitry
- Charging method, if rechargeable
- 3.3 V / 5 V regulation requirements
- Peak current capability during LoRa transmission and GNSS operation
- Mission-duration margin

```text
Battery
   │
   ▼
Protection
   │
   ▼
Voltage Regulation / Distribution
   │
   ├── ESP32-S3
   ├── RFM95W
   ├── SAM-M10Q
   ├── BME280
   ├── MPU-6050
   └── MicroSD
```

### 9.2 Onboard Data Storage

A MicroSD interface is recommended so the payload can retain:

- Timestamped environmental measurements
- GNSS position and altitude
- IMU measurements
- Detected atmospheric events
- Battery / system-health information
- Communication-health information

This allows complete mission data to survive radio interruptions or loss of telemetry coverage.

### 9.3 Battery and Power Monitoring

A voltage/current monitor such as an **INA219-, INA226-, or equivalent-class device** should be evaluated for:

- Battery voltage
- Load current
- Instantaneous power
- Energy-use estimation
- Low-battery event detection
- Autonomous power-management decisions

### 9.4 Connectors and Mechanical Integration

The final PCB/enclosure design should define:

- External BME280 connector
- Programming/debug connector
- Battery connector
- MicroSD access
- Antenna mechanical support / strain relief
- GNSS placement and sky view
- Sensor ventilation
- Cable retention
- PCB mounting points

---

## 10. Preliminary Budget Allocation

> These figures are planning allowances in CAD, not verified seller prices or purchase quotations. Existing laptop and university laboratory equipment are assumed available.

| Part | Cost |
|---|---:|
| 2× RFM95W LoRa | $58.48 |
| Freenove ESP32-S3 2-pack | $35.95 |
| 2× LoRa antennas | $23.82 |
| 2× SMA connectors | $8.06 |
| BME280 | $21.91 |
| SAM-M10Q GNSS | $21.48 |
| MPU-6050 5-pack | $17.91 |
| **Total so far** | **$187.61** |

**$312.39 remaining** 

---

## 11. Optional High-Altitude Mission — Additional Considerations

The 10–12 km stretch objective requires a separate readiness review. Parts adequate for the initial low-altitude test should not automatically be considered qualified for high altitude.

| Area | Additional considerations |
|---|---|
| BME280 | Measurement range and accuracy at low pressure and low temperature; verify suitability against expected flight profile |
| GNSS | Confirm altitude and dynamic limits and validate RF placement in the final payload |
| IMU | Verify low-temperature behavior and mechanical mounting |
| Battery | Capacity, discharge performance, protection, and thermal behavior in the expected environment |
| Enclosure | Insulation/protection while preserving valid ambient measurements and GNSS reception |
| LoRa radio | Flight-specific link budget, range, propagation, antenna installation, configuration, and packet loss |
| LoRa antenna | Verify installation performance, ground-plane effects, temperature rating, and mechanical strain relief |
| Recovery | Appropriate descent system, position tracking, trajectory prediction, and recovery plan |
| Data storage | Retain complete data throughout radio interruptions or loss of coverage |
| Structural integration | Mass, lifting capability, attachment security, and environmental robustness |

---

## 12. Open Hardware Decisions and Validation Gates

- Final lawful radio operating parameters, RF power, range, telemetry update rate, and link-health metrics.
- Required sensor accuracy, atmospheric-event definitions, detection algorithms, and sampling thresholds.
- Power budget, mission duration, battery chemistry/capacity, and measured low-temperature performance.
- MicroSD interface and storage capacity.
- Battery / current-monitoring device.
- Final payload mass, dimensions, component mounting, antenna support, and thermal design.
- Final custom PCB / carrier PCB architecture.
- External environmental-sensor connector and cable arrangement.
- Approved test/launch locations, applicable aviation and site requirements, and responsibility for related costs.
- Whether any outdoor free flight or optional 10–12 km flight will be attempted.

---

## 13. Current Baseline Scope

Build and validate an integrated autonomous sensing payload and functional ground-station dashboard with:

- ESP32-S3 onboard processing
- 915 MHz LoRa telemetry
- Atmospheric temperature, humidity, and pressure sensing
- GNSS position / altitude / timing
- Acceleration and angular-motion sensing
- Onboard data logging
- Battery and power-health monitoring
- Adaptive sensing and communication algorithms
- Ground-station telemetry logging and dashboard visualization

No propulsion, active steering, or active altitude control is required.

Development should proceed through:

1. Bench-level electronics and sensor validation
2. Ground-to-ground LoRa communication testing
3. Integrated payload testing
4. Environmental and power testing
5. Approved low-altitude tethered testing
6. Separate flight-readiness review before any free flight

