# BME688 Implementation Plan — STM32WL55 GPS/IMU/LoRa System

## 1. Implementation objective

Integrate the Bosch BME688 environmental sensor into the existing STM32WL55 dual-core firmware without disrupting GPS reception, MPU orientation measurements, or LoRa communications.

The BME688 will provide four additional environmental measurements:

- Temperature: °C
- Atmospheric pressure: Pa or hPa
- Relative humidity: %RH
- Gas resistance: Ω, indicating changes in the surrounding gas mixture

Gas resistance is not a direct CO₂ or VOC concentration measurement. Producing air-quality indices or estimated gas concentrations requires additional processing, such as Bosch's BSEC library.

### Existing system architecture

Based on the supplied firmware repository:

- Cortex-M4 (CPU1) manages GNSS reception, MPU-6500/MPU-9250 measurements, and I²C communication.
- Cortex-M0+ (CPU2) manages LoRa transmission, ACK handling, and telemetry delivery.
- Shared SRAM1 transfers sensor measurements between the two cores.
- Telemetry protocol v3 currently transmits a 26-byte GPS + IMU packet every 10 seconds.

The BME688 should follow the same architecture rather than introducing a separate communication pathway.

Proposed data flow

Sensors — I²C2 / USART1

BME688 · MPU-6500/9250 · GNSS receiver

CPU1 — Cortex-M4

Sensor drivers, measurement scheduling, validation

Shared SRAM1

BME688 snapshot + existing GPS/IMU snapshots

CPU2 — Cortex-M0+

Freshness checks, packet encoding, LoRa radio

Ground station

Decode and display GPS + IMU + environment

## 2. Hardware integration

Your pictured board exposes four I²C connections on connector P1: `VCC`, `GND`, `SCL`, and `SDA`.

The current firmware already configures I²C2 using PA11 for SDA and PA12 for SCL, targeting 100 kHz. Reuse this bus for the BME688.

| BME688 P1 | STM32WL55 connection | Purpose             |
| --------- | -------------------- | ------------------- |
| VCC       | 3.3 V                | Sensor module power |
| GND       | GND                  | Common ground       |
| SCL       | PA12 — I²C2 SCL      | I²C clock           |
| SDA       | PA11 — I²C2 SDA      | I²C data            |

Use 3.3 V as the initial supply choice, subject to the breakout manufacturer's input-voltage specification. The bare BME688 supports VDD from 1.71–3.6 V and VDDIO from 1.2–3.6 V.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.bosch-sensortec.com\&sz=32)

Bosch Sensortec



The BME688 supports two I²C addresses:

| Address | Sensor SDO connection |
| ------- | --------------------- |
| `0x76`  | GND                   |
| `0x77`  | VDDIO                 |

Implement probing of both addresses, with `0x76` checked first. The breakout appears to have onboard components and an address-selection arrangement, but the board photograph alone does not establish its default address or pull-up values.

Bosch recommends 100 nF decoupling capacitors and nominal 4.7 kΩ I²C pull-ups for a bare-chip circuit. Inspect the breakout's existing components before adding more pull-ups.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.bosch-sensortec.com\&sz=32)

Bosch Sensortec

[image](https://www.google.com/s2/favicons?domain=https://www.bosch-sensortec.com\&sz=32)

Bosch Sensortec



Hardware acceptance criteria: Both I²C lines idle high, the existing MPU still responds, and the BME688 can be detected at exactly one of its supported addresses without bus errors.

## 3. BME688 firmware driver

### Phase 1 — Driver selection and sensor initialization

Use Bosch's official [BME68x Sensor API](https://github.com/boschsensortec/BME68x_SensorAPI?utm_source=chatgpt.com) rather than writing the measurement compensation formulas from scratch.

This C library supports the BME680/BME688 family and handles calibration coefficients, temperature compensation, pressure compensation, humidity compensation, gas resistance calculations, and measurement configuration.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://github.com\&sz=32)

GitHub



Create the following files:

```
CM4/
  Core/
    Inc/
      bme688.h                 NEW
    Src/
      bme688.c                 NEW

  Drivers/
    BME68x/
      bme68x.c                 NEW
      bme68x.h                 NEW
      bme68x_defs.h            NEW
```

Keep the device-specific application wrapper in `bme688.c`; the vendor driver should remain largely unchanged.

#### Initialization sequence

1. Initialize I²C2 using the existing `MX_I2C2_Init()`.
2. Check `0x76`, then `0x77`.
3. Read the chip ID register `0xD0` and the variant ID register `0xF0`. Expect chip ID `0x61` and variant ID `0x01` for a BME688.
4. Initialize the Bosch API and load the sensor's factory calibration.
5. Configure temperature, pressure, and humidity oversampling.
6. Configure the gas heater, if gas measurements are enabled.
7. Start the non-blocking measurement scheduler.

The identification registers and forced-mode configuration are defined in the Bosch datasheet, Sections 3.5 and 5.3.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.bosch-sensortec.com\&sz=32)

Bosch Sensortec+2



### Phase 2 — Sampling configuration

For the initial implementation, use Bosch's forced measurement mode, which performs one measurement cycle and automatically returns to sleep.

| Parameter                | Starting configuration        |
| ------------------------ | ----------------------------- |
| Measurement mode         | Forced                        |
| Temperature oversampling | 2×                            |
| Pressure oversampling    | 16×                           |
| Humidity oversampling    | 1×                            |
| IIR filter               | Off initially                 |
| Sampling interval        | 1 second                      |
| Gas heater target        | 300 °C, subject to validation |
| Gas heater duration      | 100 ms, subject to validation |
| Telemetry interval       | Existing 10 seconds           |

These are proposed integration settings, not a verified optimal gas-sensing profile. Start by validating temperature, pressure, and humidity with the gas heater disabled; enable the gas heater afterward.

The Bosch API provides `bme68x_get_meas_dur()` to calculate the measurement duration. Add the heater duration and suitable scheduling margin instead of assuming a fixed conversion time.

### Phase 3 — Protect the existing MPU sampling loop

The MPU is currently polled in `CM4/Core/Src/main.c` approximately every 10 ms.

Do not add a long `HAL_Delay()` to wait for the BME688. That could disrupt the 100 Hz MPU sampling schedule and GNSS processing.

Use a small state machine:

1

IDLE

Start forced measurement when the next sample is due.

2

MEASURING

Return to the main loop while the sensor converts data. Continue MPU polling and GNSS processing.

3

READ

Retrieve and validate the completed measurement using Bosch's API.

4

PUBLISH

Update shared SRAM, record a timestamp, and schedule the next measurement.

All I²C operations should have bounded timeouts. Since the existing MPU and BME688 are both accessed from CPU1's main loop, a single serialized bus owner is sufficient initially.

Acceptance: BME688 readings arrive about once per second without significantly increasing MPU polling latency or interrupting GNSS reception.

## 4. Inter-core shared memory integration

### Phase 4 — Extend the existing SRAM mailbox

The repository currently reserves a 1 KB shared mailbox region starting at `0x20003C00`.

Its current layout is:

| SRAM offset | Content         | Size      |
| ----------- | --------------- | --------- |
| 0           | GNSS/VCP ring   | 520 bytes |
| 520         | GPS status      | 28 bytes  |
| 548         | MPU status      | 20 bytes  |
| 568         | Available space | 456 bytes |

This gives us enough room for the BME688 without relocating existing GPS or MPU snapshots.&#x20;

Pasted text.txt



Add a BME688 snapshot at offset 568 in:

`Common/GnssVcp/gnss_vcp_mailbox.h`

Proposed structure:

```

typedef struct
{
    volatile uint32_t seq;
    volatile uint32_t update_timestamp_ms;

    volatile int16_t  temperature_centi_c;
    volatile uint16_t humidity_centi_pct;
    volatile uint32_t pressure_pa;
    volatile uint32_t gas_resistance_ohms;

    volatile uint8_t  valid_flags;
    volatile uint8_t  initialized;
    volatile uint8_t  reserved[2];

} GnssVcpBme688Status;

```

With the existing ARM/C11 alignment assumptions, this structure is 24 bytes, placing the new end of the used mailbox at offset 592. Enforce size, alignment, offsets, and the 1024-byte limit with compile-time assertions.

### Shared memory rules

Follow the MPU's existing sequence-lock publication design:

- CPU1 is the only writer.
- CPU2 is the only reader.
- An odd sequence indicates an update is in progress.
- Memory barriers protect the publication order.
- CPU2 verifies matching even sequence numbers before accepting a snapshot.
- An initialization marker distinguishes absent data from a valid invalid-status publication.

Use separate validity flags for temperature/pressure/humidity and gas. A gas heater problem should not automatically make the other three measurements invalid.

### Phase 5 — CPU2 sensor freshness reader

Create:

```
CM0PLUS/Core/Inc/bme688_status.h
CM0PLUS/Core/Src/bme688_status.c
```

Model it after `mpu_status.c`, but adjust the freshness policy for a 1 Hz environmental sensor.

| Parameter                 | Proposed value       |
| ------------------------- | -------------------- |
| CPU1 publication rate     | 1 Hz                 |
| CPU2 snapshot observation | Every 100–250 ms     |
| Maximum sample age        | 3 seconds            |
| Freshness clock           | CPU2 local RTC ticks |

An important detail from the current implementation is that CPU1 uses millisecond SysTick timing while CPU2 uses approximately 1024 ticks per second. Do not subtract timestamps from different cores. Continue using CPU2-local observation timing, as the MPU snapshot reader already does.

A frozen or failing BME688 must eventually be marked stale, even when its last reading looks plausible.

## 5. LoRa telemetry protocol changes

### Phase 6 — Introduce telemetry protocol v4

The existing v3 packet contains GPS and MPU data in 26 bytes. I recommend preserving its first 26 bytes exactly and appending 13 bytes for BME688 measurements.

This minimizes changes to the existing encoder and decoder while making the environmental fields part of the same acknowledged telemetry packet.

#### Proposed v4 packet layout

| Byte offset | Field                       | Size     |
| ----------- | --------------------------- | -------- |
| 0–25        | Existing GPS + MPU payload  | 26 bytes |
| 26          | BME688 validity flags       | 1 byte   |
| 27–28       | Temperature × 100 (°C)      | 2 bytes  |
| 29–32       | Pressure (Pa)               | 4 bytes  |
| 33–34       | Relative humidity × 100 (%) | 2 bytes  |
| 35–38       | Gas resistance (Ω)          | 4 bytes  |
|             | Total                       | 39 bytes |

All multibyte values remain little-endian.

Use the validity flags as follows: bit 0 means the temperature/pressure/humidity reading is valid; bit 1 means the gas-resistance reading is valid. Other bits are reserved initially.

The packet-size increase is:

Current v3

# 26 B

Proposed v4

# 39 B

Increase

# +13 B

The existing application has a 255-byte radio buffer, so the 39-byte packet fits within its configured buffer. The actual LoRa airtime impact should still be measured using the project's radio settings.

### Code changes

Update:

`Common/Telemetry/telemetry.h`

```
#define TELEMETRY_VERSION       4u
#define TELEMETRY_PACKET_SIZE   39u
```

Retain the old v3 size constant for compatibility tests.

In `Common/Telemetry/telemetry.c`, extend `Telemetry_EncodeCombinedStatus()` and `Telemetry_DecodeCombinedStatus()` to accept a BME688 status and validate the additional fields.

Also update `CM0PLUS/SubGHz_Phy/App/subghz_phy_app.c` to read the environmental snapshot and append it during packet construction. The existing encoder call and retransmission buffer are located in the CPU2 transmission path.&#x20;

Pasted text.txt



### Preserve existing radio behavior

Do not change the fundamental communication process:

- Keep a new transmission every 10 seconds.
- Keep the 2-second ACK timeout.
- Keep sequence numbers and duplicate detection.
- Retransmit the original buffered packet, not newly sampled sensor data.
- Keep the 4-byte ACK structure, with its version updated consistently.

Both payload and ground-station firmware must understand v4 before deployment. An unmodified v3 ground station will reject the new packet, and ACK version checks must be updated alongside the data packet.

## 6. Ground-station integration

### Phase 7 — Decode and display environmental data

Update the `GROUND` role in `CM0PLUS/SubGHz_Phy/App/subghz_phy_app.c` and its shared telemetry decoder.

A successful reception should produce output similar to:

```
RX: seq=42 RSSI=-65 SNR=9

GPS: FIX
Latitude: 53.546124
Longitude: -113.493823
Altitude: 680.25 m

IMU: MPU-9250 VALID
Roll: -4.25 deg
Pitch: 12.75 deg
Heading: 125.50 deg

BME688: VALID
Temperature: 22.45 C
Pressure: 945.21 hPa
Humidity: 43.50 %
Gas resistance: 120450 ohms

ACK: sent seq=42
```

These are illustrative values, not measured output.

For the embedded VCP logger, continue using bounded integer/fixed-point formatting instead of adding floating-point `printf` support solely for environmental measurements.

If environmental data is unavailable, the ground station should display `BME688: UNAVAILABLE`, not zero-valued temperature, pressure, humidity, or gas as if they were measurements.

## 7. Exact firmware files to change

| File                                      | Required change                                          |
| ----------------------------------------- | -------------------------------------------------------- |
| `CM4/Core/Src/main.c`                     | Initialize BME688 and schedule non-blocking measurements |
| `CM4/Core/Src/i2c.c`                      | Reuse I²C2; add any needed bus diagnostics               |
| `CM4/Core/Inc/bme688.h`                   | New application-facing driver API                        |
| `CM4/Core/Src/bme688.c`                   | New Bosch API adapter and scheduler                      |
| `CM4/CMakeLists.txt`                      | Add Bosch driver and wrapper sources                     |
| `Common/GnssVcp/gnss_vcp_mailbox.h`       | Add BME688 shared snapshot                               |
| `CM0PLUS/Core/Inc/bme688_status.h`        | New CPU2 reader API                                      |
| `CM0PLUS/Core/Src/bme688_status.c`        | New freshness/validity reader                            |
| `CM0PLUS/subghz_phy.cmake`                | Compile the new CPU2 reader                              |
| `Common/Telemetry/telemetry.h`            | Define v4 fields and sizes                               |
| `Common/Telemetry/telemetry.c`            | Extend encoding and decoding                             |
| `CM0PLUS/SubGHz_Phy/App/subghz_phy_app.c` | Extend TX, RX, and ground display                        |

No new STM32 peripheral is required, and the MPU driver should remain independent of the BME688 driver.

## 8. Testing and acceptance plan

Integration acceptance checklist

0 of 16 complete

1\. Hardware detection

BME688 responds at 0x76 or 0x77

Existing MPU remains functional on I²C2

No SDA/SCL stuck-low errors

2\. Sensor measurements

Temperature, pressure, and humidity return plausible calibrated readings

Gas heater and gas-valid flags work correctly

Measurement scheduler does not stall the 100 Hz MPU polling loop

3\. Inter-core communication

CPU1 publishes coherent snapshots

CPU2 handles invalid, torn, and uninitialized snapshots

Sensor publications older than 3 seconds are rejected

4\. Telemetry and ground station

39-byte v4 packet passes round-trip and malformed-packet tests

Payload and ground-station builds both succeed

Ground station displays all measurements with correct units

5\. Regression and fault testing

GNSS and MPU remain operational when BME688 is disconnected

ACK and exact-packet retransmission still work

Repeated I²C errors do not stop the telemetry transmitter

Sensor power and pressure-range constraints are verified

### Flight-specific considerations

Because the current firmware is designed for a GNSS/LoRa balloon payload, there are two additional environmental constraints worth testing.

Pressure and temperature range: Bosch specifies the BME688 for 300–1100 hPa and −40 to +85 °C. At high balloon altitudes, pressure may fall below the characterized operating range and temperature can also exceed the supported limits. Record such samples for diagnostics if useful, but flag them as outside the specified measurement range rather than claiming guaranteed accuracy.&#x20;

[image](https://www.google.com/s2/favicons?domain=https://www.bosch-sensortec.com\&sz=32)

Bosch Sensortec



Power and thermal effects: Gas sensing uses a heated element that can create significant current peaks and influence local temperature measurements. Verify the battery/power rail during heater operation and position the sensor away from warm regulators, radios, and sealed compartments that could bias readings.

For the first flight-capable release, temperature, pressure, and humidity should function independently of the gas heater.

## 9. Recommended implementation order

Milestone 1 — Hardware and standalone driver

Confirm I²C detection, integrate the Bosch Sensor API, and read compensated temperature/pressure/humidity values through CPU1 VCP logging. Leave LoRa unchanged.

Milestone 2 — Scheduling and gas measurements

Add the state machine, test coexistence with MPU polling, and enable validated gas heater measurements.

Milestone 3 — Shared SRAM integration

Add the 24-byte snapshot, seqlock publication, CPU2 reader, freshness tracking, and unit tests.

Milestone 4 — Telemetry v4

Extend the packet to 39 bytes, update both endpoints, and preserve ACK/retry behavior.

Milestone 5 — Regression and flight-readiness validation

Run host tests, build PAYLOAD and GROUND images in Debug and Release, perform hardware disconnect/reconnect tests, and verify system stability.

## Final recommendation

The lowest-risk approach for the current project is to add the BME688 as another CPU1 I²C sensor, publish its data through a separate SRAM snapshot, and extend the existing combined telemetry packet to v4.

I would specifically avoid introducing BSEC-based air-quality classification in the first iteration. Raw, compensated temperature, pressure, humidity, and gas resistance provide a simpler integration with fewer dependencies. BSEC can be evaluated later, including target-library support and memory requirements, if classified air-quality output becomes a requirement.

The next concrete development task is Milestone 1: add `bme688.c`/`bme688.h`, integrate Bosch's API, and demonstrate reliable sensor readings alongside the existing MPU on I²C2. That establishes the hardware and driver behavior before modifying the inter-core protocol or LoRa packet format.